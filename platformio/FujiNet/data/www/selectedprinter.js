var mySelect = document.getElementById("select_printermodel1");

var opts = mySelect.options;
for (var opt, j = 0; opt = opts[j]; j++) {
	if (opt.value == current_printer) {
		mySelect.selectedIndex = j;
		break;
	}
}
