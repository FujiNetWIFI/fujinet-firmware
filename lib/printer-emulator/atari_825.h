#ifndef ATARI_825_H
#define ATARI_825_H

#include "pdf_printer.h"

class atari825 : public pdfPrinter
{
protected:
    struct epson_cmd_t
    {
        uint8_t cmd = 0;
        uint8_t N1 = 0;
        uint8_t N2 = 0;
        uint16_t N = 0;
        uint16_t ctr = 0;
    } epson_cmd;
    bool escMode = false;
    bool backMode = false;

    const uint16_t fnt_underline = 0x001;
    const uint16_t fnt_expanded = 0x002;
    const uint16_t fnt_compressed = 0x004;
    const uint16_t fnt_proportional = 0x008;

    uint16_t epson_font_mask = 0; // need to set to normal TODO

    //void print_8bit_gfx(uint8_t c);
    void not_implemented();
    void esc_not_implemented();
    void set_mode(uint16_t m);
    void clear_mode(uint16_t m);
    void reset_cmd();
    uint8_t epson_font_lookup(uint16_t code);
    double epson_font_width(uint16_t code);
    void epson_set_font(uint8_t F, double w);
    void check_font();

    virtual void pdf_clear_modes() override{};
    void pdf_handle_char(uint8_t c, uint8_t aux1, uint8_t aux2) override;
    virtual void post_new_file() override;

public:
    const char *modelname() { return "Atari 825"; };

private:
    const uint8_t char_widths_825[95] = {
        7,  //	32
        7,  //	33	!
        10, //	34	"
        15, //	35	#
        12, //	36	$
        16, //	37	%
        14, //	38	&
        7,  //	39	'
        7,  //	40	(
        7,  //	41	)
        12, //	42	*
        12, //	43	+
        7,  //	44	,
        12, //	45	-
        7,  //	46	.
        12, //	47	/
        12, //	48	0
        12, //	49	1
        12, //	50	2
        12, //	51	3
        12, //	52	4
        12, //	53	5
        12, //	54	6
        12, //	55	7
        12, //	56	8
        12, //	57	9
        7,  //	58	:
        7,  //	59	;
        12, //	60	<
        12, //	61	=
        12, //	62	>
        12, //	63	?
        14, //	64	@
        16, //	65	A
        15, //	66	B
        14, //	67	C
        16, //	68	D
        14, //	69	E
        14, //	70	F
        16, //	71	G
        16, //	72	H
        10, //	73	I
        14, //	74	J
        16, //	75	K
        14, //	76	L
        18, //	77	M
        16, //	78	N
        16, //	79	O
        14, //	80	P
        14, //	81	Q
        15, //	82	R
        12, //	83	S
        14, //	84	T
        16, //	85	U
        16, //	86	V
        18, //	87	W
        16, //	88	X
        16, //	89	Y
        10, //	90	Z
        12, //	91	[
        12, //	92  '\'
        12, //	93	]
        12, //	94	^
        12, //	95	_
        7,  //	96	`
        12, //	97	a
        12, //	98	b
        10, //	99	c
        12, //	100	d
        12, //	101	e
        10, //	102	f
        12, //	103	g
        12, //	104	h
        8,  //	105	i
        6,  //	106	j
        12, //	107	k
        8,  //	108	l
        16, //	109	m
        12, //	110	n
        12, //	111	o
        12, //	112	p
        12, //	113	q
        10, //	114	r
        12, //	115	s
        10, //	116	t
        12, //	117	u
        12, //	118	v
        16, //	119	w
        12, //	120	x
        12, //	121	y
        10, //	122	z
        10, //	123	{
        7,  //	124	|
        10, //	125	}
        12  //	126	~
    };
};

#endif