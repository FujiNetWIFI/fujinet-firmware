// //
// // https://en.wikipedia.org/wiki/Commodore_DOS
// //


// #include <cstdint>
// #include <string>
// #include <ctime>

// struct dos_command
// {
//     std::string media;
//     bool replace = false; // @ - Save Replace
//     std::string command;
//     std::string type;
//     std::string mode;
//     uint16_t datacrc = NULL;
// };

// class DOS
// {
//     public:
//         dos_command command;
//         time_t date_match_start = NULL;
//         time_t date_match_end = NULL;

//         virtual void execute(std::string command) = 0;
// };