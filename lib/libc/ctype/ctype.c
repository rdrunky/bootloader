#include <ctype.h>

int isdigit(int c)
{
	return c >= '0' && c <= '9';
}

int isupper(int c)
{
	return c >= 'A' && c <= 'Z';
}

int isxdigit(int c)
{
	return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int isprint(int c)
{
	return c >= ' ' && c <= '~';
}

int tolower(int c)
{
	return isupper(c) ? c + ('a' - 'A') : c;
}
