$disksize = 92176
$sectorsize = 128
$loader_sector_byte_loc = 1
$headerfile = "header.atr"
$programfile = "config.com"
$diskfile = "autorun.atr"

$loc = (get-location).path + "\"

$bytes_header = [io.file]::readallbytes($loc + $headerfile)
$bytes_program = [io.file]::readallbytes($loc + $programfile)
$bytes_padding =
  new-object byte[] ($disksize - $bytes_header.length - $bytes_program.length)

$loader_size_sectors_rounded =
  [convert]::ToInt16([math]::ceiling($bytes_program.length / $sectorsize))

$loader_byte = $bytes_program[$loader_sector_byte_loc]

if($loader_byte -ne $loader_size_sectors_rounded)
{
  write-output "Current loader byte (`$$($loader_byte.toString("X"))) patched to `$$($loader_size_sectors_rounded.toString("X"))"
  $bytes_program[$loader_sector_byte_loc] = $loader_size_sectors_rounded
}

[io.file]::writeallbytes
  ($loc + $diskfile, $bytes_header + $bytes_program + $bytes_padding)
