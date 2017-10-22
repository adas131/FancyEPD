#include <stdlib.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "FancyEPD.h"

#define AUTO_REFRESH_AFTER_UPDATES   (5)

static int16_t _modelWidth(epd_model_t model)
{
	switch (model) {
		case k_epd_model_E2215CS062:    return 112;
		default:                        break;
	}

	return 0;	// not found
}

static int16_t _modelHeight(epd_model_t model)
{
	switch (model) {
		case k_epd_model_E2215CS062:    return 208;
		default:                        break;
	}

	return 0;	// not found
}

// IL3895.pdf : Voltages, used in waveform phases.
// See the _sendWaveforms() for working examples.
const uint8_t _SOURCE = 0b00;	// Same, no change (actually the pixel drifts towards grey after enough updates)
const uint8_t _HIGH = 0b01;	// High (white)
const uint8_t _LOW  = 0b10;	// Low (black)

// Generate a byte describing one step of a waveform.
// Used for command: Write LUT register (0x32)
// hh = pixel transition is high-to-high,  hl = high-to-low, etc.
static uint8_t waveformByte(uint8_t hh, uint8_t hl, uint8_t lh, uint8_t ll)
{
	return (hh << 6) | (hl << 4) | (lh << 2) | ll;
}

FancyEPD::FancyEPD(epd_model_t model, uint32_t cs, uint32_t dc, uint32_t rs, uint32_t bs, uint32_t d0, uint32_t d1) : Adafruit_GFX(_modelWidth(model), _modelHeight(model))
{
	_model = model;
	_cs = cs;	// Chip select
	_dc = dc;	// Data/command
	_rs = rs;	// Register select
	_bs = bs;	// Busy signal
	_d0 = d0;
	_d1 = d1;
	_hardwareSPI = (d0 == 0xffff);
	_temperature = 0x1A;
	_borderColor = 0x0;
	_borderBit = 0x0;
	_updatesSinceRefresh = 0xFF;
	markEntireDisplayDirty();
	_prevWindow = _window;
}

bool FancyEPD::init(uint8_t * optionalBuffer, epd_image_format_t bufferFormat)
{
	// Release old buffer, if it exists
	freeBuffer();

	if (optionalBuffer) {
		_buffer = optionalBuffer;
		_didMallocBuffer = false;

	} else {
		// malloc our own buffer.
		_buffer = (uint8_t *)calloc(getBufferSize(), sizeof(uint8_t));
		if (!_buffer) return false;

		_didMallocBuffer = true;
	}

	_bufferFormat = bufferFormat;

	// SPI
	if (_hardwareSPI) {
		SPI.begin();

	} else {
		// Software SPI
		pinMode(_d0, OUTPUT);
		pinMode(_d1, OUTPUT);
	}

	pinMode(_cs, OUTPUT);
	pinMode(_dc, OUTPUT);
	pinMode(_rs, OUTPUT);
	pinMode(_bs, INPUT_PULLUP);

	// Reset the screen
	digitalWrite(_rs, HIGH);
	//delay(1);         // ZKA: required?
	digitalWrite(_cs, HIGH);

	return true;
}

uint8_t * FancyEPD::getBuffer()
{
	return _buffer;
}

uint32_t FancyEPD::getBufferSize()
{
	// Assumes 1-bit buffer
	return (WIDTH * HEIGHT) / 8;
}

void FancyEPD::clearBuffer(uint8_t color)
{
	memset(_buffer, color, getBufferSize());
	markEntireDisplayDirty();
}

// Default behavior: Only push data for changed pixels.
// Use this method to mark the entire display dirty, and
// send the entire _buffer to the EPD. (It's slower.)
void FancyEPD::markEntireDisplayDirty()
{
	_window = (window16){
		.xMin = 0,
		.yMin = 0,
		.xMax = WIDTH - 1,
		.yMax = HEIGHT - 1
	};
}

void FancyEPD::markEntireDisplayClean()
{
	_window = (window16){
		.xMin = WIDTH - 1,
		.yMin = HEIGHT - 1,
		.xMax = 0,
		.yMax = 0
	};
}

// Override Adafruit_GFX basic function for setting pixels
void FancyEPD::drawPixel(int16_t x, int16_t y, uint16_t color)
{
	if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height)) return;

	int16_t temp;
	switch (getRotation()) {
		case 1:
		{
			temp = x;
			x = (WIDTH - 1) - y;
			y = temp;
		}
		break;

		case 2:
		{
			x = (WIDTH - 1) - x;
			y = (HEIGHT - 1) - y;
		}
		break;

		case 3:
		{
			temp = x;
			x = y;
			y = (HEIGHT - 1) - temp;
		}
		break;

		default:
			break;
	}

	uint8_t *ptr = &_buffer[(x / 8) + y * ((WIDTH + 7) / 8)];

	if (color) {
		*ptr |= 0x80 >> (x & 7);
	} else {
		*ptr &= ~(0x80 >> (x & 7));
	}

	_window.xMin = min(x, _window.xMin);
	_window.yMin = min(y, _window.yMin);
	_window.xMax = max(x, _window.xMax);
	_window.yMax = max(y, _window.yMax);
}

void FancyEPD::setBorderColor(uint8_t color)
{
	_borderColor = color;
}

void FancyEPD::updateScreen(epd_update_t update_type)
{
	if (update_type == k_update_auto) {
		if (_updatesSinceRefresh < (AUTO_REFRESH_AFTER_UPDATES - 1)) {
			update_type = k_update_no_blink;
		} else {
			update_type = k_update_quick_refresh;
		}
	}

	_waitUntilNotBusy();
	_screenWillUpdate(update_type);
	_sendImageData();
	_sendUpdateActivation(update_type);
}

void FancyEPD::updateScreenWithImage(const uint8_t * data, epd_image_format_t format, epd_update_t update_type)
{
	// 1 bit (black & white)? Fall back on updateScreen()
	if (format == k_image_1bit) {
		memcpy(_buffer, data, getBufferSize());
		updateScreen(update_type);
		return;
	}

	if (update_type == k_update_auto) {
		update_type = k_update_quick_refresh;
	}

	bool do_blink = (update_type == k_update_quick_refresh) || (update_type == k_update_builtin_refresh);

	if (do_blink) {
		_waitUntilNotBusy();
		clearBuffer();
		_screenWillUpdate(update_type);
		_sendBorderBit(update_type, 0);	// white border
		_sendImageData();
		_sendUpdateActivation(update_type);

	} else {
		_waitUntilNotBusy();
		_screenWillUpdate(k_update_none);	// Don't apply waveforms here
	}

	epd_update_t draw_scheme = k_update_INTERNAL_monochrome_tree;
	_waitUntilNotBusy();
	_sendWaveforms(draw_scheme);

	// Multiple draws will be required, for each bit.
	switch(format) {
		case k_image_2bit_monochrome:
		{
			for (uint8_t b = 0; b < 2; b++) {	// least-significant bits first
				uint16_t offset = 0;
				uint8_t mask0 = 0x1 << (b + 6);
				uint8_t mask1 = 0x1 << (b + 4);
				uint8_t mask2 = 0x1 << (b + 2);
				uint8_t mask3 = 0x1 << (b);

				markEntireDisplayClean();

				for (int16_t y = 0; y < _height; y++) {
					for (int16_t x = 0; x < _width; x += 4) {
						uint8_t _byte = data[offset++];

						drawPixel(x, y, _byte & mask0);
						drawPixel(x + 1, y, _byte & mask1);
						drawPixel(x + 2, y, _byte & mask2);
						drawPixel(x + 3, y, _byte & mask3);
					}
				}

				_waitUntilNotBusy();
				_sendBorderBit(draw_scheme, (_borderColor & mask0) ? 1 : 0);
				_sendWindow();
				_sendImageData();
				_sendUpdateActivation(draw_scheme);
			}
		}
		break;

		case k_image_4bit_monochrome:
		{
			for (uint8_t b = 0; b < 4; b++) {	// least-significant bits first
				uint16_t offset = 0;
				uint8_t mask_hi = 0x1 << (b + 4);
				uint8_t mask_lo = 0x1 << b;

				markEntireDisplayClean();

				for (int16_t y = 0; y < _height; y++) {
					for (int16_t x = 0; x < _width; x += 2) {
						uint8_t _byte = data[offset++];

						// First pixel: Stored in highest 4 bits
						drawPixel(x, y, _byte & mask_hi);

						// Second pixel: Stored in lowest 4 bits
						drawPixel(x + 1, y, _byte & mask_lo);
					}
				}

				_waitUntilNotBusy();
				_sendBorderBit(draw_scheme, (_borderColor & mask_hi) ? 1 : 0);
				_sendWindow();
				_sendImageData();
				_sendUpdateActivation(draw_scheme);
			}
		}
		break;

		case k_image_8bit_monochrome:
		{
			// For the sake of brevity: We're only displaying
			// the most-significant 4 bits of each pixel.
			for (uint8_t b = 0; b < 4; b++) {	// least-significant bits first
				uint16_t offset = 0;
				uint8_t mask = 0x10 << b;

				markEntireDisplayClean();

				for (int16_t y = 0; y < _height; y++) {
					for (int16_t x = 0; x < _width; x++) {
						uint8_t _byte = data[offset++];

						drawPixel(x, y, _byte & mask);
					}
				}

				_waitUntilNotBusy();
				_sendBorderBit(draw_scheme, (_borderColor & mask) ? 1 : 0);
				_sendWindow();
				_sendImageData();
				_sendUpdateActivation(draw_scheme);
			}
		}
		break;

		default: break;
	}

	// Next `_auto` update: Trigger a refresh.
	_updatesSinceRefresh = 0xFF;
}

void FancyEPD::setTemperature(uint8_t temperature)
{
	// TODO: Temperature is a 12-bit value, so we're
	//       losing some resolution here.
	_temperature = temperature;
}

void FancyEPD::freeBuffer()
{
	if (_didMallocBuffer && _buffer) {
		free(_buffer);
		_buffer = NULL;
	}
}

// Destructor
FancyEPD::~FancyEPD()
{
	freeBuffer();
}

//
//  PRIVATE
//

void FancyEPD::_waitUntilNotBusy()
{
	// Ensure the busy pin is LOW
	while (digitalRead(_bs) == HIGH);
}

void FancyEPD::_softwareSPI(uint8_t data) {
	uint8_t mask = 0x80;

	while (mask) {
		digitalWrite(_d1, (data & mask) ? HIGH : LOW);

		digitalWrite(_d0, HIGH);
		digitalWrite(_d0, LOW);

		mask >>= 1;
	}
}

void FancyEPD::_sendData(uint8_t command, uint8_t * data, uint16_t len) {
	// 1s / 250ns clock cycle time == 4,000,000 Hz
	if (_hardwareSPI) {
		SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
	}

	digitalWrite(_cs, LOW);
	digitalWrite(_dc, LOW);

	//delay(1);	// ZKA: required?

	if (_hardwareSPI) {
		SPI.transfer(command);
	} else {
		_softwareSPI(command);
	}

	//delay(1);	// ZKA: required?

	digitalWrite(_dc, HIGH);

	//delay(1);	// ZKA: required?

	for (uint16_t i = 0; i < len; i++) {
		if (_hardwareSPI) {
			SPI.transfer(data[i]);
		} else {
			_softwareSPI(data[i]);
		}
	}

	//delay(1);	// ZKA: required?

	digitalWrite(_cs, HIGH);

	if (_hardwareSPI) {
		SPI.endTransaction();
	}
}

void FancyEPD::_screenWillUpdate(epd_update_t update_type)
{
	if ((update_type == k_update_quick_refresh) || (update_type == k_update_builtin_refresh)) {
		_updatesSinceRefresh = 0;

	} else if (_updatesSinceRefresh < 0xFF) {
		_updatesSinceRefresh++;
	}

	_sendDriverOutput();
	_sendGateScanStart();
	_sendDataEntryMode();
	_sendGateDrivingVoltage();
	_sendAnalogMode();
	_sendTemperatureSensor();
	_sendWaveforms(update_type);
	_sendBorderBit(update_type, (_borderColor & 0x80) ? 1 : 0);
	_sendWindow();
}

void FancyEPD::_sendDriverOutput()
{
	uint8_t data[] = {0xCF, 0x00};
	_sendData(0x01, data, 2);
}

void FancyEPD::_sendGateScanStart()
{
	uint8_t data[] = {0x0};
	_sendData(0x0F, data, 1);
}

void FancyEPD::_sendDataEntryMode()
{
	uint8_t data[] = {0x03};
	_sendData(0x11, data, 1);
}

void FancyEPD::_sendGateDrivingVoltage()
{
	uint8_t data[] = {0x10, 0x0A};
	_sendData(0x03, data, 2);
}

void FancyEPD::_sendAnalogMode()
{
	uint8_t data[] = {0, 0, 0};
	_sendData(0x05, data, 1);
	_sendData(0x75, data, 3);
}

void FancyEPD::_sendTemperatureSensor()
{
	uint8_t data[] = {_temperature, 0x0};
	_sendData(0x1A, data, 2);
}

void FancyEPD::_sendWaveforms(epd_update_t update_type)
{
	uint8_t lut_size = 29;
	uint8_t data[lut_size];
	memset(data, 0, lut_size);

	switch (update_type) {
		case k_update_partial:
		{
			// Apply voltage only to pixels which change.
			data[0] = waveformByte(_SOURCE, _LOW, _HIGH, _SOURCE);
			data[16] = 20;	// timing
		}
		break;

		case k_update_no_blink:
		{
			// Apply voltage to all pixels, whether they change or not.
			data[0] = waveformByte(_HIGH, _LOW, _HIGH, _LOW);
			data[16] = 20;	// timing
		}
		break;

		case k_update_quick_refresh:
		{
			data[0] = waveformByte(_LOW, _HIGH, _LOW, _HIGH);	// inverted image
			data[1] = waveformByte(_HIGH, _LOW, _HIGH, _LOW);	// normal image

			data[16] = 12;	// Inverted image: short flash
			data[17] = 20;	// Normal image: apply longer
		}
		break;

		case k_update_INTERNAL_monochrome_tree:
		{
			data[0] = waveformByte(_HIGH, _LOW, _HIGH, _LOW);
			data[16] = 3;	// Short pulse
		}
		break;

		case k_update_builtin_refresh:
		default:
		{
			// builtin_refresh: Waveforms are reset in _sendUpdateActivation()
			return;
		}
	}

	_sendData(0x32, data, lut_size);

	// Shorter dummy line period?
	/*
	uint8_t zero = 0;
	_sendData(0x3A, &zero, 1);
	*/
}

void FancyEPD::_sendBorderBit(epd_update_t update_type, uint8_t newBit)
{
	uint8_t borderByte = 0x80 | (_borderBit << 1) | newBit;

	// _partial update: Looks bad if the border bit doesn't
	// change. So apply voltage in that case.
	if (update_type == k_update_partial) {
		// Force a change. Make A1 the opposite of A0.
		borderByte = (borderByte & (~0b10)) | (((~newBit) << 1) & 0b10);
	}

	_sendData(0x3C, &borderByte, 1);

	_borderBit = newBit;
}

void FancyEPD::_sendImageData()
{
	// To defeat double-buffering artifacts on the device:
	// Send enough pixels to cover _prevWindow and _window.
	int16_t xMinByte = min(_prevWindow.xMin, _window.xMin) >> 3;
	int16_t xMaxByte = max(_prevWindow.xMax, _window.xMax) >> 3;
	int16_t yMin = min(_prevWindow.yMin, _window.yMin);
	int16_t yMax = max(_prevWindow.yMax, _window.yMax);

	// Send this many bytes of image data:
	uint16_t len = ((xMaxByte - xMinByte) + 1) * ((yMax - yMin) + 1);

	// Window: Do not allocate another buffer. We may
	// not have enough RAM. (Using Teensy LC, for example.)
	// Instead: Arrange pixel bytes so stream starts at [0].
	bool doArrange = (xMinByte > 0) ||
	                 (xMaxByte < ((WIDTH - 1) >> 3)) ||
	                 (yMin > 0) ||
	                 (yMax < (HEIGHT - 1));

	if (doArrange) {
		_swapBufferBytes(xMinByte, yMin, xMaxByte, yMax, true);
	}

	_sendData(0x24, &_buffer[0], len);

	// After sending: Swap everything back.
	if (doArrange) {
		_swapBufferBytes(xMinByte, yMin, xMaxByte, yMax, false);
	}
}

void FancyEPD::_sendUpdateActivation(epd_update_t update_type)
{
	uint8_t sequence = 0xC7;
	if (update_type == k_update_builtin_refresh) {
		sequence = 0xF7;
	}

	_sendData(0x22, &sequence, 1);	// Display Update type

	_sendData(0x20, NULL, 0);	// Master activation

	// To defeat double-buffering artifacts: Cache _window
	// as _prevWindow.
	_prevWindow = _window;

	// The screen pixels now match _buffer. All clean!
	markEntireDisplayClean();
}

void FancyEPD::_sendWindow()
{
	// Multiplexing: Only MUX the rows which have changed
	int16_t muxLines = max(16, _window.yMax - _window.yMin + 1);
	uint8_t data_mux[] = {(uint8_t)muxLines, 0x0};
	_sendData(0x01, data_mux, 2);

	uint8_t gateStartY = min(_window.yMin, HEIGHT - (muxLines - 1));
	_sendData(0x0F, &gateStartY, 1);

	// Window for image data: Send enough pixels to cover
	// both _prevWindow and _window, defeat double-buffering
	// artifacts on the device
	int16_t xMin = min(_prevWindow.xMin, _window.xMin);
	int16_t xMax = max(_prevWindow.xMax, _window.xMax);
	int16_t yMin = min(_prevWindow.yMin, _window.yMin);
	int16_t yMax = max(_prevWindow.yMax, _window.yMax);

	// Window coordinates
	uint8_t data_x[] = {
		(uint8_t)(xMin >> 3),
		(uint8_t)(xMax >> 3)
	};
	_sendData(0x44, data_x, 2);

	uint8_t data_y[] = {
		(uint8_t)(yMin),
		(uint8_t)(yMax)
	};
	_sendData(0x45, data_y, 2);

	// XY counter
	_sendData(0x4E, &data_x[0], 1);
	_sendData(0x4F, &data_y[0], 1);
}

// For streaming windowed region, without allocating another
// buffer: Arrange pixel bytes so stream starts at &_buffer[0].
void FancyEPD::_swapBufferBytes(int16_t xMinByte, int16_t yMin, int16_t xMaxByte, int16_t yMax, bool ascending)
{
	uint16_t len = ((xMaxByte - xMinByte) + 1) * ((yMax - yMin) + 1);

	if (ascending) {
		// Arrange windowed bytes starting at [0]
		uint16_t b = 0;

		for (int16_t y = yMin; y <= yMax; y++) {
			uint16_t win = y * (WIDTH >> 3) + xMinByte;	// optimization

			for (int16_t x = xMinByte; x <= xMaxByte; x++) {
				uint8_t temp = _buffer[b];
				_buffer[b] = _buffer[win];
				_buffer[win] = temp;

				b++;
				win++;
			}
		}

	} else {	// descending
		uint16_t b = len - 1;

		for (int16_t y = yMax; y >= yMin; y--) {
			uint16_t win = y * (WIDTH >> 3) + xMaxByte;

			for (int16_t x = xMaxByte; x >= xMinByte; x--) {
				uint8_t temp = _buffer[b];
				_buffer[b] = _buffer[win];
				_buffer[win] = temp;

				b--;
				win--;
			}
		}
	}
}
