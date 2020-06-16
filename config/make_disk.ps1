$disksize = 92176
$headerfile = "header.atr"
$programfile = "config.com"
$diskfile = "autorun.atr"

$loc = (get-location).path + "\"

$bytes_header = [io.file]::readallbytes($loc + $headerfile)
$bytes_program = [io.file]::readallbytes($loc + $programfile)
$bytes_padding = new-object byte[] ($disksize - $bytes_header.length - $bytes_program.length)

[io.file]::writeallbytes($loc + $diskfile, $bytes_header + $bytes_program + $bytes_padding)
