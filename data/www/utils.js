function writeLocaleNumber(num) {
	document.write(num.toLocaleString());
}

function writeUptimeString(secs) {

	var mins = secs / 60 | 0;
	var hours = mins / 60 | 0;
	var days = hours / 24 | 0;

	var result = '';

	if (days)
		result += days + ' days, ';
	if (hours % 24)
		result += (hours % 24) + ' hours, ';
	if (mins % 60)
		result += (mins % 60) + ' minutes, ';
	if (secs % 60)
		result += (secs % 60) + ' seconds';

	document.write(result);
}
