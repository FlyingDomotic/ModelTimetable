(function() {
	// Get element by id
	function $id(id) {
		return document.getElementById(id);
	}

	// File drag hover
	function fileDragHover(e) {
		e.stopPropagation();
		e.preventDefault();
		e.target.className = (e.type == "dragover" ? "hover" : "");
	}

	// Drop file selection
	function fileSelectHandler(e) {
		// Cancel event and hover styling
		fileDragHover(e);
		// Fetch FileList object
		var files = e.target.files || e.dataTransfer.files;
		// Process all File objects
		for (var i = 0, f; f = files[i]; i++) {
			sendOneFile(f);
		}
	}

	// Send one file
	function sendOneFile(file) {
        // Send a post request for onle file
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/upload');
        // création de l'objet FormData
        var formData = new FormData();
        formData.append('file', file);
        xhr.send(formData);
	}

	// Initialize
	function initDropFile() {
		console.log("Initializing dropFile");
		var fileselect = $id("fileSelect");
		var filedrag = $id("fileDrag");
		// File select and submit button
		fileselect.addEventListener("change", fileSelectHandler, false);

		// Is XHR2 available?
		var xhr = new XMLHttpRequest();
		if (xhr.upload) {
			filedrag.addEventListener("dragover", fileDragHover, false);
			filedrag.addEventListener("dragleave", fileDragHover, false);
			filedrag.addEventListener("drop", fileSelectHandler, false);
			filedrag.style.display = "block";
		}
    }
	// Call initialization file
	if (window.File && window.FileList && window.FileReader) {
		initDropFile();
	}
})();                
