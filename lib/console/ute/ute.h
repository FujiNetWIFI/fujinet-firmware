//
//  uTE - Micro Text Editor
//

#pragma once

// Version code
#define VERSION "1.0.1"

//#define ENABLE_HIGHLIGHT
//#define ENABLE_UNDOREDO
//#define ENABLE_SEARCH

#ifdef ENABLE_HIGHLIGHT
// Highlight flags
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
#endif


/*** Edit actions ***/
enum ActionType {
    CutLine,
    PasteLine,
    FlipUp,
    FlipDown,
    NewLine,
    InsertChar,
    DelChar,
};
typedef enum ActionType ActionType;

typedef struct Action Action;
struct Action {
    ActionType t;
    int cpos_x;
    int cpos_y;
    bool cursor_on_tilde;
    char* string;
};

#ifdef ENABLE_UNDOREDO
// Max Undo/Redo Operations
// Set to -1 for unlimited Undo
// Set to 0 to disable Undo
#define ACTIONS_LIST_MAX_SIZE 80

typedef struct AListNode AListNode;
struct AListNode {
    Action* action;
    AListNode* next;
    AListNode* prev;
};

typedef struct ActionList ActionList;
struct ActionList {
    AListNode* head;
    AListNode* tail;
    AListNode* current;
    int size;
};
#endif

// uTE - Micro Text Editor
int ute(int argc, char **argv);
