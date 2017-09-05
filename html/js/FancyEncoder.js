function logBytes(ar)
{
	var str = "";
	while (ar.length) {
		str += "\t";

		var chunk = ar.splice(0, 20);

		while (chunk.length) {
			str += ("   " + chunk.shift()).substr(-3) + ",";
		}

		str += "\n";
	}

	console.log(str);
}

function logBytes_signed(ar)
{
	var str = "";
	while (ar.length) {
		str += "\t";

		var chunk = ar.splice(0, 12);

		while (chunk.length) {
			str += ("   " + chunk.shift()).substr(-4) + ",";
		}

		str += "\n";
	}

	console.log(str);
}

function FancyEncoder(canvas, format)
{
	var self = this;

	var data = canvas.getContext('2d').getImageData(0, 0, canvas.width, canvas.height).data;

	var ar = [];

	switch (format) {
		case "8bpp_monochrome_raw":
		{
			// 8-bit color, 1 pixel per byte
			for (var i = 0; i < data.length; i += 4) {
				ar.push(0xff - data[i]);
			}
		}
		break;

		case "4bpp_monochrome_raw":
		{
			// 4-bit color, 2 pixels per byte
			for (var i = 0; i < data.length; i += 8) {
				var byte = (data[i] & 0xf0) | ((data[i + 4] & 0xf0) >> 4);
				byte = 0xff - byte;
				ar.push(byte);
			}
		}
		break;

		case "8bpp_palette":
		{
			var bgColor = 0x444444;
			var white = 0xffffff;
			var pal = [bgColor, 0x0, white];

			function palLookup(hex) {
				for (var p = 0; p < pal.length; p++) {
					if (hex == pal[p]) return p;
				}
				console.log("add:", hex.toString(16));
				pal.push(hex);
				return pal.length - 1;
			}

			var byte = 0x0;
			for (var i = 0; i < data.length; i += 4) {
				var hex = (data[i] << 16) | (data[i+1] << 8) | (data[i+2]);
				var p0 = palLookup(hex);

				ar.push(p0);
			}

			/*
			console.log(_.map(pal, function(p){
				return "0x" + p.toString(16);
			}).join(", "));
			console.log("};");
			*/

			function color565(hex) {
				return ((hex & 0xf80000) >> 8) |
				       ((hex & 0x00fc00) >> 5) |
				       ((hex & 0x0000f8) >> 3);
			}

			console.log("static const uint16_t EYE_PALETTE[] = {");
			console.log(_.map(pal, function(p){
				return "0x" + color565(p).toString(16);
			}).join(", "));
			console.log("};\n")

			console.log("static const uint8_t IRIS_COLORS[] = {");
			logBytes(ar);
			console.log("};\n")

			// Map of distance to edge. Used for skewing texture
			// to reveal pupil. To reduce flash memory size:
			// only build the upper-left quarter of the map.
			var map = [];
			var distanceBytes = [];

			for (var y = 0; y < canvas.height / 2; y++) {
				for (var x = 0; x < canvas.width / 2; x++) {
					var dx = x - (canvas.width / 2);
					var dy = y - (canvas.height / 2);
					var dist = Math.sqrt((dx * dx) + (dy * dy));

					// Outside the circle?
					if (dist >= (canvas.width / 2)) {
						map.push(0, 0);
						distanceBytes.push(0);

					} else {
						var angle = Math.atan2(dy, dx);
						var invDist = 1.0 - (dist / (canvas.width / 2));
						var distInt8 = invDist * 0x7f;
						map.push(-Math.round(Math.cos(angle) * distInt8));
						map.push(-Math.round(Math.sin(angle) * distInt8));
						distanceBytes.push(Math.round(invDist * 0xff));
					}
				}
			}

			console.log("static const int8_t IRIS_ANGLES[] = {");
			logBytes(map);
			console.log("};\n");

			console.log("static const uint8_t IRIS_DISTANCES[] = {");
			logBytes(distanceBytes);
			console.log("};\n");

			// Need sclera circle. Store as 1bpp?
			var sclera = [];
			var byte = 0x0;
			var mask = 0x80;
			var y = 0;
			while (y < 110) {
				var x = 0;
				while (x < 110) {

					var dx = (110 - x);
					var dy = (110 - y);
					var outside = Math.sqrt((dx * dx) + (dy * dy)) > 110;

					if (outside) byte |= mask;

					mask >>= 1;
					if (!mask) {
						sclera.push(byte);
						byte = 0x0;
						mask = 0x80;
					}

					x++;
				}
				y++;
			}

			if (mask != 0x80) sclera.push(byte);

			console.log("static const uint8_t SCLERA_1BPP[] = {");
			logBytes(sclera);
			console.log("};\n");

			return;
		}
		break;

		case "2bpp_monochrome_raw":
		{
			// 2-bit color, 4 pixels per byte
			for (var i = 0; i < data.length; i += 16) {
				var byte = (data[i] & 0xc0) | ((data[i + 4] & 0xc0) >> 2) | ((data[i + 8] & 0xc0) >> 4) | ((data[i + 12] & 0xc0) >> 6);
				byte = 0xff - byte;
				ar.push(byte);
			}
		}
		break;

		case "1bpp_monochrome_raw":
		{
			// 1-bit color, 8 pixels per byte
			for (var i = 0; i < data.length; i += 32) {
				var byte = ((data[i     ] & 0x80)     ) |
				           ((data[i +  4] & 0x80) >> 1) |
				           ((data[i +  8] & 0x80) >> 2) |
				           ((data[i + 12] & 0x80) >> 3) |
				           ((data[i + 16] & 0x80) >> 4) |
				           ((data[i + 20] & 0x80) >> 5) |
				           ((data[i + 24] & 0x80) >> 6) |
				           ((data[i + 28] & 0x80) >> 7);
				byte = 0xff - byte;
				ar.push(byte);
			}
		}
		break;

		case "1bpp_monochrome_rle_vlq":
		{
			var binImg = BinaryImage(canvas, 0x10);
			var rle = RLE(binImg);
			var vlq = VLQ(rle, 2);	// 2: black, and white.

			ar = vlq;
		}
		break;

		case "1bpp_monochrome_rle_xor_vlq":
		{
			var binImg = BinaryImage(canvas, 0x80);

			// XOR version of the image. Pixels are XOR'd
			// compared to the pixel directly above them.
			var w = canvas.width;
			var h = canvas.height;
			var xorImg = [];

			// First row is identical
			for (var i = 0; i < w; i++) {
				xorImg.push(binImg[i]);
			}

			var offset = w;
			for (var j = 1; j < h; j++) {
				for (var i = 0; i < w; i++) {
					var isSame = (binImg[offset] == binImg[offset - w]);
					xorImg.push(isSame ? 0 : 1);
					offset++;
				}
			}

			var rle = RLE(xorImg);
			console.log(rle);
			var vlq = VLQ(rle, 2);	// 2: same, and xor.

			ar = vlq;
		}
		break;

		case "1bpp_monochrome_terrain_vlq":
		{
			var binImg = BinaryImage(canvas, 0x10);
			var terr = new TerrainEncoding(binImg, canvas.width);

			// Make a VLQ array
			var data = [];
			var slope = 0;
			var distance = 0;
			for (var i = 0; i < terr.length; i++) {
				var step = terr[i];

				// Differences
				var slopeDiff = wrap(step.slope - slope, 256);
				if (slopeDiff >= 128) {
					slopeDiff -= 256;
				}
				data.push(slopeDiff);

				var stepDiff = step.distance - distance;
				data.push(stepDiff);

				/*
				var slope = step.slope;
				if (slope >= 128) slope -= 256;
				data.push(slope);
				data.push(step.distance);
				*/
			}

			console.log("data", data);

			var vlq = VLQ(data, 2);
			ar = vlq;

			// Debug: Show bin img
			var cvs = document.createElement('canvas');
			cvs.width = canvas.width;
			cvs.height = canvas.height;

			var ctx = cvs.getContext('2d');
			var data = ctx.getImageData(0, 0, cvs.width, cvs.height).data;

			var d = 0;
			for (var y = 0; y < cvs.height; y++) {
				for (var x = 0; x < cvs.width; x++) {
					data[d] = data[d + 1] = data[d + 2] = (binImg[d / 4] ? 0xff : 0x0);	// red, green, blue
					data[d + 3] = 0xff;	// alpha

					d += 4;
				}
			}

			var imgData = new ImageData(data, cvs.width, cvs.height);

			ctx.putImageData(imgData, 0, 0, 0, 0, cvs.width, cvs.height);

			document.body.appendChild(cvs);
		}
		break;

		default:
		{
			console.log("** FancyEncoder: unrecognized format:", format);
		}
		break;

	}

	console.log(format, ":: LENGTH:", ar.length);

	logBytes(ar);
}
