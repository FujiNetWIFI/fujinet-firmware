/* fuji_mailbox.h -- FujiNet cartridge mailbox layout for the Atari 2600, shared by the cart
 * firmware and the MAME device; testrom/fujinet.inc mirrors it by hand.
 *
 * The edge has no write strobe, but the 6507 drives D0-D7 during a store: the cart never
 * drives a write-only page and keeps the second-to-last data sample while the address is
 * stable. A read of a write port decodes like a write, so a register is armed (FN_H_REGSEL +
 * n) and then committed (FN_H_COMMIT), and fujimail.c disarms after one use. */

#ifndef FUJI_MAILBOX_H
#define FUJI_MAILBOX_H

/* The shared registers, magics and codes. Not fuji_mailbox_check.h: the 4K window puts
 * the reply slices, status page and hotspots elsewhere, and fujimail.c uses only names. */
#include "fuji_mailbox_proto.h"

/* ---------------- address map ---------------- */

/* Console addresses; A12 high is the whole chip select.
 *   $1000-$17FF  2K banked    client code, bank selected by FN_HOT_BANK
 *   $1800-$1AFF  768 B        six 128-byte text planes
 *   $1B00-$1CFF  512 B        reply window (2 slices x 512)
 *   $1D00-$1DFF  write-only   control: arm, commit, bank, render, swap
 *   $1E00-$1EFF  write-only   TX stream: a write anywhere appends its data
 *   $1F00-$1FFF  256 B        status, claim, fixed tail, vectors */

#define FN_WINDOW_BASE   0x1000   /* A12 high: the cartridge is selected     */
#define FN_WINDOW_SIZE   0x1000   /* 4K, and there is no more                */

#define FN_BANK_BASE     0x1000   /* the banked low half                     */
#define FN_BANK_SIZE     0x0800   /* 2K per bank                             */
#define FN_FIXED_BASE    0x1800   /* the fixed high half: everything below   */

/* ---------------- cart -> console: the text planes ----------------
 *
 * The 2600 has no framebuffer, so the cart renders text into six 128-byte planes, one per GRP
 * write of the 48-pixel kernel; plane p holds text columns 2p and 2p+1. A plane is indexed by
 * scanline, Y = row * FN_T_CELL_H + line, and its 128-byte alignment keeps `lda $1800,y` at
 * four cycles: a kernel that varies its timing tears.
 *
 * Bit 7 is the leftmost pixel: the left column in bits 7-5 with gap bit 4, the right in bits
 * 3-1 with gap bit 0. A 3x5 glyph in a 4x6 cell.
 */
#define FN_T_BASE        0x1800
#define FN_T_PLANES      6
#define FN_T_PLANE_LEN   0x80     /* 128-byte alignment is load-bearing      */
#define FN_T_PLANE(p)    (FN_T_BASE + (p) * FN_T_PLANE_LEN)
#define FN_T_COLS        12       /* 12 x 4px = the 48-pixel player span     */
#define FN_T_CELL_H      6        /* 5 rows of ink, 1 of leading             */
#define FN_T_ROWS        21       /* 21 * 6 = 126 <= 128                     */

/* ---------------- cart -> console: painted memory ---------------- */

/* 512 so 5cardstud's largest reply (418 bytes) fits one slice; 2 slices keep FUJIMAIL_RX_MAX
 * at 1024. */
#define FN_R_DATA        0x1B00   /* the reply slice                         */
#define FN_R_SLICE_LEN   0x200    /* 512 bytes...                            */
#define FN_R_NSLICES     2        /* ...x 2 = FUJIMAIL_RX_MAX                */

#define FN_R_BASE        0x1F00   /* first painted status byte               */
#define FN_R_ACKSEQ      0x1F00   /* echoes SEQ when the reply is ready      */
#define FN_R_STATUS      0x1F01   /* bit0 link up, bit1 busy                 */
#define FN_R_ERR         0x1F02   /* fb_status_t of the last transaction     */
#define FN_R_REPLY_CMD   0x1F03   /* 0x06 ACK / 0x15 NAK                     */
#define FN_R_RXLEN_LO    0x1F04   /* total reply length, LE                  */
#define FN_R_RXLEN_HI    0x1F05
#define FN_R_BOOT_STATE  0x1F06
#define FN_R_BOOT_PCT    0x1F07   /* 0-100, for a MOUNT_IMAGE progress bar   */
#define FN_R_BOOT_ERR    0x1F08
#define FN_R_MAGIC0      0x1F09   /* 'F' -- cart presence check              */
#define FN_R_MAGIC1      0x1F0A   /* 'N'                                     */
#define FN_R_PROTO_VER   0x1F0B
#define FN_PROTO_VER     2        /* 2 = banking, as on the Astrocade        */
#define FN_R_SLICE_ECHO  0x1F0C   /* published LAST after every repaint      */

#define FN_R_PAINT_END   0x1F0D   /* paint covers [FN_R_DATA, FN_R_PAINT_END) */

/* Published by the bus-serving layer, deliberately just past the paint span
 * so fujimail.c never clears them. */
#define FN_B_TEXTGEN     0x1F0D   /* last row rendered, plus a toggle bit    */
#define FN_B_BANK        0x1F0E   /* the live bank -- a stray read can move  */
                                  /* it, so a careful client can notice      */
#define FN_B_FLAGS       0x1F0F   /* bit0 armed, bit1 claim honored          */

/* "FUJI" here marks an image as a FujiNet client, so the mailbox stays live after it boots; a
 * game without it leaves the mailbox dead. A 2600 image fills the window, so the claim is four
 * bytes of the image's own top page. */
#define FN_R_CLAIM       0x1F10
#define FN_R_HDR         0x1F14   /* bank count, cell height, layout rev     */

/* The path buffer's length: SET_DEVICE_FULLPATH takes the directory and the filename in one
 * 256-byte payload, so once the cart has emitted the directory the client pads the rest. */
#define FN_B_PATHLEN     0x1F17   /* 2 bytes, little-endian                  */

/* Bumped once a blit has landed. There is one blit slot and no queue, so poll this for a
 * change before the next blit (FN_B_TEXTGEN does the same for a text row); MAME runs the blit
 * inside the store and hides a missed poll. */
#define FN_B_BLITGEN     0x1F19

/* $1F20-$1FFB is the client's fixed tail: the cold stub, the bank
 * trampolines, and whatever else must be reachable from every bank.
 * $1FFC-$1FFD is the RESET vector and $1FFE-$1FFF the BRK vector. */
#define FN_FIXED_TAIL    0x1F20
#define FN_VEC_RESET     0x1FFC
#define FN_VEC_BRK       0x1FFE

/* The reply and RXLEN are repainted only by a SEQ commit or an RXSLICE select, so a client may
 * stream reply bytes straight into the TX page. With 128 bytes of RAM, the stack mirrored into
 * them, a directory browser fits no other way. */

/* ---------------- console -> cart: writes ---------------- */

/* Page $1D: the cart never drives it, which is what lets it sample the store. Offsets below
 * 0x80 arm a register; 0x80 and up are one-shot operations for the bus layer, which must act
 * before the next fetch, so none goes through the ring (fujimail.c ignores that half). */
#define FN_H_REGSEL      0x1D00   /* + n (n < 0x80): arm register n          */
#define FN_H_PAGE_MASK   0xFF00

/* Never console addresses: fujimail.c's switch only needs page numbers distinct from
 * FN_H_REGSEL, and keeping these out of the window saves two of its sixteen pages. */
#define FN_H_REGDATA     0x2E00   /* synthesized by a store to FN_H_COMMIT   */
#define FN_H_DATA        0x2F00   /* synthesized by a store anywhere in $1E  */
#define FN_H_PATHTX      0x3000   /* the path buffer, expanded into FN_H_DATA*/
#define FN_H_PATHRAW     0x3100   /* the same, without the padding           */

#define FN_TX_BASE       0x1E00   /* the real console page behind FN_H_DATA  */

/* One-shot operations, all in the bit7-set half of the control page. */
#define FN_HOT_BANK      0x80     /* + b: select bank b (b <= 0x6F)          */
#define FN_HOT_BANK_LAST 0xEF
#define FN_APP_MAX_PAGES 112

#define FN_HOT_TROW      0xF0     /* data = row: begin a row, reset cursor   */
#define FN_HOT_TCHR      0xF1     /* data = char: append, cursor saturates   */
#define FN_HOT_TEND      0xF2     /* data = attribute: render into the planes */

/* The blit port: six stores move bytes the console has neither the RAM nor the raster time to
 * move, from the reply window (src offset) into the text planes (dst offset), transformed on
 * the way. Issue it in vblank or overscan. */
#define FN_HOT_BLIT_SL   0xF5     /* source offset, low                      */
#define FN_HOT_BLIT_SH   0xF6     /* source offset, high                     */
#define FN_HOT_BLIT_DL   0xF7     /* destination offset, low                 */
#define FN_HOT_BLIT_DH   0xF8     /* destination offset, high                */
#define FN_HOT_BLIT_CNT  0xF9     /* count                                   */
#define FN_HOT_BLIT_GO   0xFA     /* data = transform; fire                  */
#define FN_BLIT_RAW      0        /* copy bytes unchanged                    */
#define FN_BLIT_TEXT     1        /* ASCII -> packed glyph pairs             */
#define FN_BLIT_FIELD    2        /* a 10x10 game field -> ten text rows      */
#define FN_BLIT_HULLS    3        /* five ship placements -> a hull overlay   */
#define FN_BLIT_SEA      4        /* fill the composed board with open sea    */
#define FN_BLIT_CELL     5        /* board[cnt] = the low byte of src         */
#define FN_BLIT_PAINT    6        /* paint the composed board into ten rows   */
#define FN_BLIT_PATH     7        /* cnt chars of the active path buffer at
                                     src, into text row dst                  */
#define FN_BLIT_TCELL    8        /* ONE character (low byte of src) into ONE
                                     cell, dst = row * FN_T_COLS + col       */
#define FN_BLIT_CARD     9        /* one seat's five cards, src = the reply
                                     offset of hand[11], dst = the TOP text
                                     row of the pair                         */
#define FN_CARD_SLOTS    5        /* five cards in a stud hand               */
#define FN_CARD_PITCH    6        /* pixels per card: 5 of art plus a gap    */
#define FN_CARD_PLANES   4        /* planes 0-3 -- pixels 0-31, the card bed */
#define FN_CARD_INK_W    5        /* pixels of art in a card                 */
#define FN_CARD_X0       2        /* left margin -- keeps ink off pixel 7,
                                     which no kernel can draw. See below.    */
#define FN_CARD_HIDE0    0x01     /* cnt: draw card 0 face-down regardless   */
#define FN_BOARD_DIM     10
#define FN_BOARD_CELLS   100
#define FN_BLIT_NOCUR    0xFF     /* FIELD: cnt = the cursor cell, or this    */

/* The playfield board, for Battleship's four-quadrant client: two 10x10 boards fill the 40-bit
 * asymmetric playfield, a cell being two PF bits. Plane r holds register r's tables (0-2 are
 * PF0/PF1/PF2 of the left half, 3-5 of the right) at bytes 30-89, so a client in board mode
 * never renders text rows 5-14. One table per kind -- HIT, MID (hit or miss) and AUX (hulls,
 * the cursor) -- indexed by cell row, 0-9 the top pair and 10-19 the bottom; the kernel draws
 * each kind on its own lines of a cell in its own color. Per half: cells 0-1 -> PF0 bits 4-5 /
 * 6-7, 2-5 -> PF1 bits 7-6 / 5-4 / 3-2 / 1-0, 6-9 -> PF2 bits 0-1 / 2-3 / 4-5 / 6-7. A slot is
 * pairrow * 2 + half. Table bytes are written whole, so the kernel never reads a half-built
 * one. */
#define FN_BLIT_PFCLR    10       /* dst = slot, src low byte = a kind mask:
                                     clear those kinds' bits in the slot    */
#define FN_BLIT_PFIELD   11       /* src = reply offset of gamefield[100],
                                     dst = slot: HIT and MID from the cells
                                     (0 sea, 1 hit, 2 miss); AUX untouched   */
#define FN_BLIT_PFHULL   12       /* src = reply offset of cnt placements
                                     (pos + 100*dir), dst = slot: OR into AUX */
#define FN_BLIT_PFCELL   13       /* dst = slot, cnt = cell 0-99, src low
                                     byte = a kind mask; bit 7 set clears    */
#define FN_PF_ROW0       30       /* the first table byte of every plane     */
#define FN_PF_KIND_LEN   20       /* entries per table: cell rows 0-19       */
#define FN_PF_KINDS      3
#define FN_PF_HIT        0
#define FN_PF_MID        1
#define FN_PF_AUX        2
#define FN_PFM_HIT       0x01     /* the kind masks PFCLR and PFCELL take    */
#define FN_PFM_MID       0x02
#define FN_PFM_AUX       0x04
#define FN_PFM_ALL       0x07
#define FN_PFM_CLEAR     0x80     /* PFCELL: clear the bits instead of set   */
#define FN_PF_SLOTS      4
#define FN_PF_TAB(r, k)  (FN_T_PLANE(r) + FN_PF_ROW0 + (k) * FN_PF_KIND_LEN)

/* One raw byte into the planes, bounded to them. The planes are the only cart memory a client
 * can read back, so a game that uses all 128 bytes of RAM (fujinet-2600-dodgem) keeps state
 * here. Four stores and a FN_B_BLITGEN poll: for state that changes a few times a second. */
#define FN_BLIT_POKE     14       /* plane[dst] = the low byte of src        */

/* A block of raw bytes from the selected path buffer into the planes. FN_HOT_PATH_CH appends a
 * byte in one store with no poll, so a client streams cnt bytes in and spends one blit, where
 * FN_BLIT_POKE costs a poll per byte (fujinet-2600-warlords needs about 17 bytes a tick).
 * Bounded to path_len, and to the planes, past which lie the reply window and status page.
 * Routed by the caller, like FN_BLIT_PATH, because its source is the path buffer. */
#define FN_BLIT_PATHPOKE 15       /* plane[dst..dst+cnt) = path[src..src+cnt) */

/* A packed tile bitset into the playfield tables, for fujinet-maze-war's 20x19 grid. The
 * tables are already a 20-column playfield (slots 0 and 1, entry = row), so pf_byte()
 * addresses it unchanged. The source is the server's bitset straight from the reply window:
 * idx = y * FN_TILE_W + x, byte idx / 8, bit idx % 8, least significant first. dst is a kind
 * mask, since a row spans both halves; cnt is rows, 0 = FN_TILE_H. FN_BLIT_PFTCELL changes one
 * cell without resending the grid; bit 7 of its column is FN_PFM_CLEAR. */
#define FN_BLIT_PFTILE   16       /* src = reply offset of a packed bitset,
                                     dst = kind mask, cnt = rows (0 = all)   */
#define FN_BLIT_PFTCELL  17       /* dst = kind mask, cnt = row, src low byte
                                     = column; FN_PFM_CLEAR in it clears     */

/* The same grid out of a path buffer, routed by the caller like FN_BLIT_PATH. A reply is gone
 * after the next SEQ commit; a path buffer survives RESET, so a client can recompose its grid
 * later, or draw one from ROM with no server. */
#define FN_BLIT_PATHTILE 18       /* dst = kind mask, cnt = rows, src = byte
                                     offset into the selected path buffer    */
#define FN_TILE_W        20       /* cells across: the whole playfield       */
#define FN_TILE_H        19       /* rows, of the 20 the tables hold         */
#define FN_TILE_BYTES    48       /* (FN_TILE_W * FN_TILE_H + 7) / 8         */

/* The M.U.L.E. map, for fujinet-multiplayer-mule's clients/atari-2600: 9 x 5 plots of 16 color
 * clocks by 32 scanlines, composed here from the server's MuleMap. Six tables, one per PF
 * register (the left half's, then the right's), at plane byte FN_MULE_MAP0 + y, where
 * y = plot row * 16 + entry; written whole from a composed copy. The console's kernel colors
 * each entry of a plot row:
 *   0-3    the owner's bracket, top: seat 0 outermost
 *   4-8    the M.U.L.E.'s good as a 4x5 glyph, the store, or FN_MULEMAP_PROD's production bars
 *   9-11   the ground: 1-3 mountains (terrain bits 2-3), the river's dots, the store's base
 *   12-15  the owner's bracket, bottom: seat 0 outermost
 * clients/atari-2600/tools/mulemap.py is the model; host_test compares. */
#define FN_BLIT_MULEMAP  19       /* src = reply offset of MuleMap (225 bytes),
                                     dst = FN_MULEMAP_* flags                 */
#define FN_MULEMAP_PROD  0x01     /* the picture is production, not glyphs   */
#define FN_MULE_MAP0     12       /* plane byte of entry 0                   */
#define FN_MULE_ENTRIES  80       /* 5 plot rows x 16                        */
#define FN_MULE_MAPLEN   225      /* sizeof(MuleMap)                         */

/* FN_BLIT_FIELD paints a Battleship gamefield (100 bytes at y*10+x in the reply window) as ten
 * text rows: column 0 the row digit, 1-10 the cells, the cursor cell as FN_CELL_CUR (cnt =
 * FN_BLIT_NOCUR to blink it off). SEA, CELL and PAINT split composing from painting, so a
 * client can build a board that is not in the reply: the placement screen's ships. */

/* FN_BLIT_CARD: five cards on a six-pixel pitch, five of art and one gap, at pixels 2-30
 * (planes 0-3); planes 4-5 stay free for a name or a purse. Suit pips need five pixels, so
 * cards ignore the cell grid. FN_CARD_X0 is 2 because pixel 7, plane 0 bit 0, is the one pixel
 * no 48-pixel kernel can draw: its late GRP writes span 33 pixels against 32 available. A card
 * is two text rows, rank on the upper row's ink lines and suit on the lower's; the sixth line,
 * where the kernel reprograms color, is written blank, which also clears a stale hand.
 * hand[11] is five rank-suit pairs ("23456789tjqka", "hdcs") and a NUL, the first NUL rank
 * ending the hand. "??" draws a card back, and FN_CARD_HIDE0 forces slot 0 face-down. Ranks
 * reuse the font's 3x5 glyph, centered; ten draws as "T". */
#define FN_CELL_SEA      '.'      /* 0: unknown water                        */
#define FN_CELL_HIT      'X'      /* 1                                       */
#define FN_CELL_MISS     'O'      /* 2                                       */
#define FN_CELL_HULL     '#'      /* painted by FN_BLIT_HULLS                */
#define FN_CELL_CUR      '+'      /* the cursor, over whatever was there     */

/* The path buffers. SET_DEVICE_FULLPATH and OPEN_DIRECTORY read exactly 256 bytes and the
 * console has 128 of RAM, so the cart holds the strings, built a character at a time and
 * emitted into the TX stream. FN_PATH_POP keeps the trailing '/', so ".." needs no client
 * state. The buffers are cart state and survive a console RESET; a client resets them. */

/* Four buffers, because CONFIG needs four strings live at once:
 *   0  the working directory (the SSID during WiFi setup, before anything is mounted)
 *   1  the directory filter (the passphrase); OPEN_DIRECTORY sends both in one block
 *   2  a pending copy's source path
 *   3  the edit scratch the on-screen keyboard types into, so an edit can be canceled
 * SEED copies the selected buffer into the scratch and COMMIT copies it back; cancel is
 * neither. COPY_FILE needs no basename op: fujiDevice appends the source's basename when the
 * destination ends in '/'. The pages are write-only, so FN_BLIT_PATH renders a buffer into a
 * text row for the client to see, and FN_BLIT_TCELL changes one character, the '>' cursor,
 * without needing the row's text. */
#define FN_HOT_PATH_CH   0xF3     /* data = char: append to the path buffer  */
#define FN_HOT_PATH_OP   0xF4     /* data = FN_PATH_*: act on the buffer     */
#define FN_PATH_RST      0        /* empty it                                */
#define FN_PATH_POP      1        /* drop the last component, keeping "/"    */
#define FN_PATH_TX       2        /* emit it into TX, NUL-padded to 256      */
#define FN_PATH_TXRAW    3        /* emit just its bytes; the client pads    */
#define FN_PATH_POPCH    4        /* drop ONE character                      */
#define FN_PATH_SEL0     5        /* subsequent ops act on buffer 0          */
#define FN_PATH_SEL1     6        /* ...on buffer 1                          */
#define FN_PATH_SEL2     7        /* ...on buffer 2                          */
#define FN_PATH_SEL3     8        /* ...on buffer 3, the edit scratch        */
#define FN_PATH_COMMIT   9        /* selected <- the edit scratch            */
#define FN_PATH_SEED     10       /* the edit scratch <- selected            */
#define FN_PATH_MAX      256
#define FN_PATH_BUFS     4
#define FN_PATH_SCRATCH  3        /* the buffer COMMIT and SEED work through */

#define FN_HOT_ARM1      0xFC     /* data must be FN_ARM_MAGIC1, then...     */
#define FN_HOT_ARM2      0xFD     /* ...FN_ARM_MAGIC2: decode goes live      */
#define FN_HOT_SWAP      0xFE     /* serve the staged image; armed-only      */
#define FN_HOT_COMMIT    0xFF     /* commit the armed register with data     */

#define FN_H_COMMIT      (FN_H_REGSEL + FN_HOT_COMMIT)   /* $1DFF */

/* The arming gate: pages $1D and $1E are always tri-stated, and the decode stays dead until
 * two ordered stores carry these values, so a 7800 BIOS probing cart space (why PlusCart gates
 * $1FF4) trips nothing. Disarmed when a game without the claim boots. */
#define FN_ARM_MAGIC1    0xB5
#define FN_ARM_MAGIC2    0x4A

/* The FN_H_DATA stream, as on every sibling: NPARAM x { size (1|2|4), that many bytes, LE },
 * then the payload; at most FN_TX_MAX. A write anywhere in $1E00-$1EFF appends, and
 * `sta $1E00,x` cannot carry, so its dummy read lands on the same inert page. */

/* Only STA/STX/STY may target $1D00-$1EFF: a read-modify-write puts three cycles on the
 * address (read, old value, new value), and the cart cannot tell which sample to keep.
 * tools/checkrom.py fails any client that does otherwise; the 6507 has no interrupts, so that
 * static check is complete. */

/* Booting a staged image, from the client:
 *   1. poll FN_R_BOOT_STATE until FN_BOOT_READY;
 *   2. write FN_REG_BOOTLOCK = FN_BOOTLOCK_MAGIC;
 *   3. copy the swap stub into zero page and JMP to it;
 *   4. the stub stores anything to $1DFE, and the cart swaps the window before the next fetch,
 *      which is in RAM with A12 low;
 *   5. the stub does JMP ($FFFC), the new image's reset vector.
 * The stub shares the 128 bytes of RAM with the stack, so the copier checks SP. It also
 * silences AUDV0/AUDV1 and sets SWACNT/SWBCNT back to inputs, which few games do. */

/* A client takes its next SEQ from FN_R_ACKSEQ + 1 (255 wraps to 1), never from a counter
 * in RAM: a console RESET restarts the client and the cart cannot see it. RESET is
 * survivable from any bank because $1FFC is in the fixed half and points at a stub that
 * selects bank 0. */

#endif /* FUJI_MAILBOX_H */
