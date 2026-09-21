/**
 * ftp_dir_parse - turn the buffered reply to an FTP LIST into entries.
 *
 * Split out of fnFTP::read_directory so it depends only on the standard
 * library and ftpparse, never on fnFTP.h -> Protocol.h -> bus.h. That
 * is what lets ftp_directory_tests drive it with a literal listing instead
 * of a socket, which is the only way the EOF and blank-line edges are
 * cheap to pin down (fujinet-firmware #1652).
 */

#ifndef FTP_DIR_PARSE_H
#define FTP_DIR_PARSE_H

#include <istream>
#include <string>

#include "global_types.h"

/**
 * Pull the next usable entry out of an FTP LIST stream.
 *
 * Blank lines and lines ftpparse cannot make sense of ("total 14786",
 * the VMS banners) are skipped rather than surfaced, so every FUJI_ERROR::NONE
 * return carries a real entry. The caller loops until it gets something else,
 * so running out of entries must NOT report NONE.
 *
 * @param listing the buffered LIST reply, consumed a line at a time
 * @param name    output: entry name, symlink target stripped
 * @param filesize output: size in octets, 0 when the format did not say
 * @param is_dir  output: true when the entry is worth a CWD
 * @return FUJI_ERROR::NONE when an entry was parsed, FUJI_ERROR::UNSPECIFIED
 *         once the listing is exhausted
 */
fujiError_t ftp_next_dir_entry(std::istream &listing, std::string &name,
                               long &filesize, bool &is_dir);

#endif /* FTP_DIR_PARSE_H */
