#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define REGEXP 1

#define case_map(c) toupper(c)

int fnmatch (char *p, char *s)
	       /* sh pattern to match */
		/* string to match it to */
/* Recursively compare the sh pattern p with the string s and return 1 if
   they match, and 0 or 2 if they don't or if there is a syntax error in the
   pattern.  This routine recurses on itself no deeper than the number of
   characters in the pattern. */
{
    int c;			/* pattern char or start of range in [-] loop */

    /* Get first character, the pattern for new fnmatch calls follows */
    c = *p++;

    /* If that was the end of the pattern, match if string empty too */
    if (c == 0)
	return *s == 0;

    /* '?' (or '%') matches any character (but not an empty string) */
#ifdef REGEXP
    if (c == '?')
#else
    if (c == '?' || c == '%')
#endif
	return *s ? fnmatch (p, s + 1) : 0;

    /* '*' matches any number of characters, including zero */
    if (c == '*')
    {
	if (*p == 0)
	    return 1;
	for (; *s; s++)
	    if ((c = fnmatch (p, s)) != 0)
		return c;
	return 2;		/* 2 means give up--shmatch will return false */
    }
#ifdef REGEXP
    /* Parse and process the list of characters and ranges in brackets */
    if (c == '[')
    {
	int e;			/* flag true if next char to be taken literally */
	char *q;		/* pointer to end of [-] group */
	int r;			/* flag true to match anything but the range */

	if (*s == 0)		/* need a character to match */
	    return 0;
	p += (r = (*p == '!' || *p == '^'));	/* see if reverse */
	for (q = p, e = 0; *q; q++)	/* find closing bracket */
	    if (e)
		e = 0;
	    else if (*q == '\\')
		e = 1;
	    else if (*q == ']')
		break;
	if (*q != ']')		/* nothing matches if bad syntax */
	    return 0;
	for (c = 0, e = *p == '-'; p < q; p++)	/* go through the list */
	{
	    if (e == 0 && *p == '\\')	/* set escape flag if \ */
		e = 1;
	    else if (e == 0 && *p == '-')	/* set start of range if - */
		c = *(p - 1);
	    else
	    {
		if (*(p + 1) != '-')
		    for (c = c ? c : *p; c <= *p; c++)	/* compare range */
			if (case_map (c) == case_map (*s))
			    return r ? 0 : fnmatch (q + 1, s + 1);
		c = e = 0;	/* clear range, escape flags */
	    }
	}
	return r ? fnmatch (q + 1, s + 1) : 0;		/* bracket match failed */
    }
#endif
    /* If escape ('\'), just compare next character */
    if (c == '\\')
	if ((c = *p++) == 0)	/* if \ at end, then syntax error */
	    return 0;

    /* Just a character--compare it */
    return case_map (c) == case_map (*s) ? fnmatch (p, ++s) : 0;
}

