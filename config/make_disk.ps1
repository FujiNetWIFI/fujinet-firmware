$programfile = "config.com"
$diskfile = "autorun.atr"

$sectorsize = 128
$sectorcount = 720

$loader_sector_byte_loc = 1

$disksize = $sectorsize * $sectorcount
$diskparagraphs = $disksize / 16

# bytes 1, 2 = magic number 0x0296
# bytes 3, 4 = disk size in paragraphs (16 bytes)
# bytes 5, 6 = sector size
# bytes 7-16 = unused

[byte[]] $atr_header = 0x96, 0x02,
	($diskparagraphs -band 0xFF), ($diskparagraphs -shr 8 -band 0xFF),
	($sectorsize -band 0xFF), ($sectorsize -shr 8 -band 0xFF),
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0

$loc = (get-location).path + [System.IO.Path]::DirectorySeparatorChar

$bytes_program = [io.file]::readallbytes($loc + $programfile)
$bytes_padding =
  new-object byte[] ($disksize - $bytes_program.length)

$loader_size_sectors_rounded =
  [convert]::ToInt16([math]::ceiling($bytes_program.length / $sectorsize))

$loader_byte = $bytes_program[$loader_sector_byte_loc]

if($loader_byte -ne $loader_size_sectors_rounded)
{
  write-output "Current loader byte (`$$($loader_byte.toString("X"))) patched to `$$($loader_size_sectors_rounded.toString("X"))"

  $bytes_program[$loader_sector_byte_loc] = $loader_size_sectors_rounded
}

[io.file]::writeallbytes($loc + $diskfile,
  $atr_header + $bytes_program + $bytes_padding)
