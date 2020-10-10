var mySelect = document.getElementById("select_printermodel1");

var opts = mySelect.options;
for (var opt, j = 0; opt = opts[j]; j++) {
	if (opt.value == current_printer) {
		mySelect.selectedIndex = j;
		break;
	}
}

var mySelect = document.getElementById("select_printerport1");

var opts = mySelect.options;
for (var opt, j = 0; opt = opts[j]; j++) {
	if (opt.value == current_printerport) {
		mySelect.selectedIndex = j;
		break;
	}
}

var mySelect = document.getElementById("select_hsioindex");

var opts = mySelect.options;
for (var opt, j = 0; opt = opts[j]; j++) {
	if (opt.value == current_hsioindex) {
		mySelect.selectedIndex = j;
		break;
	}
}

var mySelect = document.getElementById("select_rotation_sounds");

var opts = mySelect.options;
for (var opt, j = 0; opt = opts[j]; j++) {
	if (opt.value == current_rotation_sounds) {
		mySelect.selectedIndex = j;
		break;
	}
}

function changeTz() {
	var sel = document.getElementById("select_tz").value;
	document.getElementById("txt_timezone").value = sel;
}

function writeLocaleNumber(num) {
	document.write(num.toLocaleString());
}
