$(document).ready(function(){

	// Relative luminance of each color channel (sRGB)
	const LUMA_R = 0.2126;
	const LUMA_G = 0.7152;
	const LUMA_B = 0.0722;

	var img = null;
	var file_name = null;

	const SCREENS = {
		"E2215CS062": {dims:[112, 208]},
	};

	const PALETTES = {
		"1bit": {name:"1-bit (black and white)"},
		"2bit_mono": {name:"2-bit grayscale"},
		"4bit_mono": {name:"4-bit grayscale"},
	};

	_.each(Object.keys(SCREENS), function(key){
		var dims = SCREENS[key].dims;

		// Add an <option> to the dropdown
		$('#screen').append($("<option></option>")
			.attr("value", key)
			.text(key + " [" + dims.join(",") + "]"));
	});

	_.each(Object.keys(PALETTES), function(key){
		$('#palette').append($("<option></option>")
			.attr("value", key)
			.text(PALETTES[key].name));
	});

	function dropzoneError(err, file) {
		alert("dropzoneError: " + err);
	}

	function onDropzoneData(file) {
		console.log("onDropzoneData:", file);

		// Sanitize image name: remove weird characters, etc
		file_name = file.name;
		file_name = file_name.replace(/\..*$/, "");	// remove extension
		file_name = file_name.replace(/\s/g, "_");	// spaces to underscores
		file_name = file_name.replace(/^([^A-Za-z_])/, "_$1");	// first char must be valid for C variable names

		img = new Image;
		img.onload = redrawImage;
		img.src = URL.createObjectURL(file);
	}

	function getOrientation() {
		console.log("it's", $("input[name=orientation]:checked").val());
		return $("input[name=orientation]:checked").val();
	}

	function getWidth() {
		var screen = SCREENS[$('#screen').val()];
		var ori = getOrientation();
		return (ori === "portrait") ? screen.dims[0] : screen.dims[1];
	}

	function getHeight() {
		var screen = SCREENS[$('#screen').val()];
		var ori = getOrientation();
		return (ori === "portrait") ? screen.dims[1] : screen.dims[0];
	}

	function resizeDropzone() {
		var w = getWidth();
		var h = getHeight();
		$("#dropzone").css({"width": w+"px", "height": h+"px"});
		$("#drop_canvas").attr({"width": w, "height": h});

		redrawImage();
	}

	function redrawImage() {
		if (!img) return;

		$("#drop_help").remove();

		var w = getWidth();
		var h = getHeight();

		// Resize to cover the screen. Align centered.
		var scale = Math.max(w / img.width, h / img.height);
		var drawX = (w - (img.width * scale)) / 2;
		var drawY = (h - (img.height * scale)) / 2;

		// Draw to canvas
		var canvas = $('#drop_canvas').get(0);
		var ctx = canvas.getContext('2d');
		ctx.drawImage(img, drawX, drawY, img.width * scale, img.height * scale);

		// Convert to the desired palette
		var pal = $("#palette").val();

		// Brightness values will be rounded off to nearest
		// palette color (4 bits == 16 colors, etc.)
		var lum_steps = 255;
		if (pal === "1bit") lum_steps = 2;
		if (pal === "2bit_mono") lum_steps = 4;
		if (pal === "4bit_mono") lum_steps = 16;

		var imgData = ctx.getImageData(0, 0, w, h);
		var data = imgData.data;

		// Code values will be stored here
		var cValues = [];

		// Each pixel has 4 bytes: RGBA.
		for (var i = 0; i < data.length; i += 4) {
			var lum = 0.0;	// Will have range 0..255
			lum += data[i    ] * LUMA_R;
			lum += data[i + 1] * LUMA_G;
			lum += data[i + 2] * LUMA_B;

			// Round off mono to the nearest palette color
			lum = Math.round(lum * ((lum_steps - 1) / 255.0));
			cValues.push(lum);

			// Screen preview color
			lum *= (255.0 / (lum_steps - 1));
			data[i] = data[i + 1] = data[i + 2] = lum;
			data[i + 3] = 255;	// alpha
		}

		// Draw this to <canvas>
		ctx.putImageData(imgData, 0, 0);

		// Print the C code
		var format;
		if (pal === "1bit") format = "1bpp_monochrome_raw";
		if (pal === "2bit_mono") format = "2bpp_monochrome_raw";
		if (pal === "4bit_mono") format = "4bpp_monochrome_raw";

		// Encode the image as uint8_t array
		var encoder = new FancyEncoder(canvas, format);

		// Share the code block
		var code = "static const uint8_t " + file_name + "[] = {\n";
		code += encoder.output;
		code += "};"
		$("#c_code").val(code);

		// Usage example
		var palette_enum = "k_image_none";
		if (pal === "1bit") palette_enum = "k_image_1bit";
		if (pal === "2bit_mono") palette_enum = "k_image_2bit_monochrome";
		if (pal === "4bit_mono") palette_enum = "k_image_4bit_monochrome";

		var usage = "epd.updateScreenWithImage( " + file_name + ", " + palette_enum + " );"
		$("#usage_example").text(usage);

		$("#codeSuccess").show();
		$("#copy_result").text("");
	}

	function copyCodeToClipboard() {
		$("#c_code").select();

		try {
			var success = document.execCommand("copy");
			$("#copy_result").text("Copied!")

		} catch(err) {
			$("#copy_result").text("Oops, unable to copy.")
		}
	}

	$("#dropzone").filedrop({
		fallback_id: 'upload_button',
		fallback_dropzoneClick : true,
		error: dropzoneError,
		allowedfiletypes: ['image/jpeg','image/png','image/gif'],
		allowedfileextensions: ['.jpg','.jpeg','.png','.gif'],
		maxfiles: 1,
		maxfilesize: 20,	// max file size in MBs
		beforeEach: onDropzoneData
	});

	$("screen").on("change", resizeDropzone);
	$("input[name=orientation]").on("change", resizeDropzone);
	$("#palette").on("change", redrawImage);
	$("#copy_to_clipboard").on("click", copyCodeToClipboard);

	// Init
	resizeDropzone();

});
