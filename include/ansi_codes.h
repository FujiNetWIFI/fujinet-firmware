/*
 * This is free and unencumbered software released into the public domain.
 *
 * For more information, please refer to <https://unlicense.org>
 *
 * https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a
 *
 */

/*
    Color Codes, Escapes & Languages

    I was able to use colors in my terminal by using a variety of different escape values. 
    As the conversation implies above, different languages require different escapes, 
    furthermore; there are several different sequences that are implemented for ANSI escapes, 
    and they can vary quite a bit. Some escapes have a cleaner sequence than others. 
    Personally I like the \e way of writing an escape, as it is clean and simple. 
    However, I couldn't get it to work anywhere, save the BASH scripting language.

    Each escape works with its adjacent language

        \x1b  👉‍    Node.js
        \x1b  👉‍    Node.js w/ TS
        \033  👉‍    GNU Cpp
        \033  👉‍    ANSI C
        \e    👉‍    BASH
*/

//Regular text
#define ANSI_BLACK "\e[0;30m"
#define ANSI_RED "\e[0;31m"
#define ANSI_GREEN "\e[0;32m"
#define ANSI_YELLOW "\e[0;33m"
#define ANSI_BLUE "\e[0;34m"
#define ANSI_MAGENTA "\e[0;35m"
#define ANSI_CYAN "\e[0;36m"
#define ANSI_WHITE "\e[0;37m"

//Regular bold text
#define ANSI_BLACK_BOLD "\e[1;30m"
#define ANSI_RED_BOLD "\e[1;31m"
#define ANSI_GREEN_BOLD "\e[1;32m"
#define ANSI_YELLOW_BOLD "\e[1;33m"
#define ANSI_BLUE_BOLD "\e[1;34m"
#define ANSI_MAGENTA_BOLD "\e[1;35m"
#define ANSI_CYAN_BOLD "\e[1;36m"
#define ANSI_WHITE_BOLD "\e[1;37m"

//Regular dim text
#define ANSI_BLACK_DIM "\e[2;30m"
#define ANSI_RED_DIM "\e[2;31m"
#define ANSI_GREEN_DIM "\e[2;32m"
#define ANSI_YELLOW_DIM "\e[2;33m"
#define ANSI_BLUE_DIM "\e[2;34m"
#define ANSI_MAGENTA_DIM "\e[2;35m"
#define ANSI_CYAN_DIM "\e[2;36m"
#define ANSI_WHITE_DIM "\e[2;37m"

//Regular italics text
#define ANSI_BLACK_ITALICS "\e[3;30m"
#define ANSI_RED_ITALICS "\e[3;31m"
#define ANSI_GREEN_ITALICS "\e[3;32m"
#define ANSI_YELLOW_ITALICS "\e[3;33m"
#define ANSI_BLUE_ITALICS "\e[3;34m"
#define ANSI_MAGENTA_ITALICS "\e[3;35m"
#define ANSI_CYAN_ITALICS "\e[3;36m"
#define ANSI_WHITE_ITALICS "\e[3;37m"

//Regular underline text
#define ANSI_BLACK_UNDERLINE "\e[4;30m"
#define ANSI_RED_UNDERLINE "\e[4;31m"
#define ANSI_GREEN_UNDERLINE "\e[4;32m"
#define ANSI_YELLOW_UNDERLINE "\e[4;33m"
#define ANSI_BLUE_UNDERLINE "\e[4;34m"
#define ANSI_MAGENTA_UNDERLINE "\e[4;35m"
#define ANSI_CYAN_UNDERLINE "\e[4;36m"
#define ANSI_WHITE_UNDERLINE "\e[4;37m"

//Regular reversed text
#define ANSI_BLACK_REVERSED "\e[7;30m"
#define ANSI_RED_REVERSED "\e[7;31m"
#define ANSI_GREEN_REVERSED "\e[7;32m"
#define ANSI_YELLOW_REVERSED "\e[7;33m"
#define ANSI_BLUE_REVERSED "\e[7;34m"
#define ANSI_MAGENTA_REVERSED "\e[7;35m"
#define ANSI_CYAN_REVERSED "\e[7;36m"
#define ANSI_WHITE_REVERSED "\e[7;37m"

//Regular background
#define ANSI_BLACK_BACKGROUND "\e[40m"
#define ANSI_RED_BACKGROUND "\e[41m"
#define ANSI_GREEN_BACKGROUND "\e[42m"
#define ANSI_YELLOW_BACKGROUND "\e[43m"
#define ANSI_BLUE_BACKGROUND "\e[44m"
#define ANSI_MAGENTA_BACKGROUND "\e[45m"
#define ANSI_CYAN_BACKGROUND "\e[46m"
#define ANSI_WHITE_BACKGROUND "\e[47m"

//High intensty background 
#define ANSI_BLACK_HIGH_INTENSITY_BACKGROUND "\e[0;100m"
#define ANSI_RED_HIGH_INTENSITY_BACKGROUND "\e[0;101m"
#define ANSI_GREEN_HIGH_INTENSITY_BACKGROUND "\e[0;102m"
#define ANSI_YELLOW_HIGH_INTENSITY_BACKGROUND "\e[0;103m"
#define ANSI_BLUE_HIGH_INTENSITY_BACKGROUND "\e[0;104m"
#define ANSI_MAGENTA_HIGH_INTENSITY_BACKGROUND "\e[0;105m"
#define ANSI_CYAN_HIGH_INTENSITY_BACKGROUND "\e[0;106m"
#define ANSI_WHITE_HIGH_INTENSITY_BACKGROUND "\e[0;107m"

//High intensty text
#define ANSI_BLACK_HIGH_INTENSITY "\e[0;90m"
#define ANSI_RED_HIGH_INTENSITY "\e[0;91m"
#define ANSI_GREEN_HIGH_INTENSITY "\e[0;92m"
#define ANSI_YELLOW_HIGH_INTENSITY "\e[0;93m"
#define ANSI_BLUE_HIGH_INTENSITY "\e[0;94m"
#define ANSI_MAGENTA_HIGH_INTENSITY "\e[0;95m"
#define ANSI_CYAN_HIGH_INTENSITY "\e[0;96m"
#define ANSI_WHITE_HIGH_INTENSITY "\e[0;97m"

//Bold high intensity text
#define ANSI_BLACK_BOLD_HIGH_INTENSITY "\e[1;90m"
#define ANSI_RED_BOLD_HIGH_INTENSITY "\e[1;91m"
#define ANSI_GREEN_BOLD_HIGH_INTENSITY "\e[1;92m"
#define ANSI_YELLOW_BOLD_HIGH_INTENSITY "\e[1;93m"
#define ANSI_BLUE_BOLD_HIGH_INTENSITY "\e[1;94m"
#define ANSI_MAGENTA_BOLD_HIGH_INTENSITY "\e[1;95m"
#define ANSI_CYAN_BOLD_HIGH_INTENSITY "\e[1;96m"
#define ANSI_WHITE_BOLD_HIGH_INTENSITY "\e[1;97m"

//Reset
#define ANSI_RESET "\e[0m"
#define ANSI_RESET_NL "\e[0m\n"