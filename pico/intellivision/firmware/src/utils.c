
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

char* trim(char *s) {
   while (*s == ' ') s++;

   char *end = s + strlen(s) - 1;
   while (end > s && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) {
      *end = '\0';
      end--;
   }

   return s;
}

bool stralpha(char *s) {

   if (*s == '\0') 
      return false;

   while (*s) {
      if (isalnum((unsigned char)*s))
         return true;
      s++;
   }

   return false;
}

void to_lower(char *str) {
    for (int i = 0; str[i]; i++)
        str[i] = tolower((unsigned char)str[i]);
}
