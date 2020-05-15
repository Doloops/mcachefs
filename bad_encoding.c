#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_SPECIAL_SIZE 16
#define MAX_LINE_SIZE 4096

#ifdef __SPE_ARRAY
#define MAX_SPECIALS 2048
char all_specials[MAX_SPECIALS][MAX_SPECIAL_SIZE];
#endif

struct spetree
{
    struct spetree* next[256];
    char value[MAX_SPECIAL_SIZE];
    char target[MAX_SPECIAL_SIZE];
    char *line;
};

struct spetree root_spetree;

char *current_line_in_spetree = NULL;

void dump_spe(const char* special)
{
    for (const char* s = special ; *s ; s++ )
    {
	fprintf(stderr, " [%x]", *s);
    }
    fprintf(stderr, "\n %s\n", special);
}

struct spetree* find_spetree(const char* special, int* length)
{
    struct spetree* spt = &root_spetree;
    for ( const char* s = special ; *s ; s++ )
    {
	if ( spt->next[*s] == NULL )
	{
	    struct spetree* next = malloc(sizeof(struct spetree));
	    memset(next, 0, sizeof(struct spetree));
	    spt->next[*s] = next;
	}
	spt = spt->next[*s];
	(*length) ++;
    }
    return spt;
}

void register_special(const char* value, const char* target)
{
    int length = 0;
    struct spetree* spt = find_spetree(value, &length);
    strncpy(spt->value, value, MAX_SPECIAL_SIZE);
    strncpy(spt->target, target, MAX_SPECIAL_SIZE);
    fprintf(stderr, "Register at spt=%p\n", spt);
    dump_spe(spt->value);
    dump_spe(spt->target);
}

char* add_special(char* special, char* line)
{
    int u;
    int length = 0;
    struct spetree *spt = find_spetree(special, &length);
    // fprintf(stderr, "Add %s, spt=%p, spt->value=%s, spt->target=%s, length=%d\n", special, spt, spt->value, spt->target, length);
    if ( spt->value[0] )
    {
	if ( spt->target[0] )
	{
	    // fprintf(stderr, "Target %s line %s\n", spt->target, line);
	    char* s = special;
	    for ( const char* t = spt->target ; *t ; t++ )
	    {
		*s++ = *t;
	    }
	    *s = 0;
	    return s;
	}
	return special + length;
    }
    strncpy(spt->value, special, MAX_SPECIAL_SIZE);
    if ( current_line_in_spetree == NULL )
    {
	current_line_in_spetree = (char*) malloc ( MAX_LINE_SIZE );
    }
    strncpy(current_line_in_spetree, line, MAX_LINE_SIZE);
    spt->line = current_line_in_spetree;

    // fprintf(stderr, "At line '%s' :\n", line);
    // dump_spe(special);
#ifdef _SPE_ARRAY
    for ( u = 0 ; u < MAX_SPECIALS ; u++ )
    {
	if ( all_specials[u][0] == 0 )
	    break;
	if ( strncmp(special, all_specials[u], MAX_SPECIAL_SIZE) == 0 )
	    return;
    }
    strncpy(all_specials[u], special, MAX_SPECIAL_SIZE);
#endif
    return special + length;
}

void dump_spetree(struct spetree* spt)
{
    if ( !spt->target[0] && spt->value[0] )
    {
	fprintf(stderr, "\nSpecial :");
	dump_spe(spt->value);
	fprintf(stderr, "%s\n", spt->line);
    }
    for ( int i = 0 ; i < 256 ; i++ )
    {
	if ( spt->next[i] )
	{
	    dump_spetree(spt->next[i]);
	}
    }
}

void dump_all_specials()
{
    fprintf(stderr, "All specials !\n");
    dump_spetree(&root_spetree);
}

#ifdef _SPE_ARRAY
void dump_all_specials_array()
{
    for ( int u = 0 ; u < MAX_SPECIALS ; u++ )
    {
	if ( all_specials[u][0] == 0 )
	    break;
	// fprintf(stderr, "** %s\n", all_specials[u]);
	dump_spe(all_specials[u]);
    }
}
#endif

int main(int argc, char** argv)
{
#ifdef _SPE_ARRAY
    for ( int u = 0 ; u < MAX_SPECIALS ; u++ )
    {
	all_specials[u][0] = 0;
    }
#endif
    memset(&root_spetree, 0, sizeof(struct spetree));
    register_special("\x80", "\xc3\x87"); // é
    register_special("\x81", "\xc3\xbc"); // é
    register_special("\x82", "\xc3\xa9"); // é
    register_special("\x83", "\xc3\xa2");
    register_special("\x84", "\xc3\xa4");
    register_special("\x85", "\xc3\xa0");
    register_special("\x86", "\xc3\xa5");
    register_special("\x87", "\xc3\xa7");
    register_special("\x88", "\xc3\xa8");
    register_special("\x8A", "\xc3\x80"); // è
    register_special("\x8C", "\xc3\xae"); // è


    register_special("\x8B", "\xc3\xaf");
    register_special("\x91", "\xc2\xab"); // ï
    register_special("\x92", "\xc2\xbb"); // ï
    register_special("\x96", "\xc3\xbb"); // ï

    register_special("\xc2\x82", "\xc3\xa9"); // é
    register_special("\xc2\x8a", "\xc3\x80"); // è
    register_special("\xc2\x8b", "\xc3\xbb"); // ï

    register_special("\xb0", "\xc2\xb0"); // °
    register_special("\xb4", "'"); // °

    register_special("\xa4", "\xc3\xb1");

    register_special("\xa8\xb0", "\xc3\xb3");
    register_special("\xe7\xf5", "\xc3\xa7");


    // Direct utf8 encodings
    // register_special("\xe0", "\xc3\xe0"); // à
    // register_special("\xe2", "\xc3\xa2"); // â

    register_special("\xa1", "\xc2\xa1");

    register_special("\xb7", "\xc2\xb6");

    register_special("\xc0", "\xc3\x80");
    register_special("\xc7", "\xc3\x87");
    register_special("\xc9", "\xc3\x89");
    register_special("\xca", "\xc3\x8a");

    register_special("\xd3", "\xc3\xa0"); 
    register_special("\xd9", "\xc3\x99"); 
    register_special("\xde", "\xc3\xa8");  // Spurious ?
    register_special("\xdf", "\xc3\x9f"); 
    register_special("\xda", "\xc3\x9a"); 

    register_special("\xe0", "\xc3\xa0");
    register_special("\xe1", "\xc3\xa1");
    register_special("\xe2", "\xc3\xa2");
    register_special("\xe3", "\xc3\xa3");
    register_special("\xe4", "\xc3\xa4");
    register_special("\xe4\xe4", "\xc3\xa4\xc3\xa4");
    register_special("\xe5", "\xc3\xa5");
    register_special("\xe6", "\xc3\xa6");
    register_special("\xe7", "\xc3\xa7");
    register_special("\xe8", "\xc3\xa8");
    register_special("\xe9", "\xc3\xa9");
    register_special("\xea", "\xc3\xaa");
    register_special("\xeb", "\xc3\xab");
    register_special("\xec", "\xc3\xac");
    register_special("\xed", "\xc3\xad");
    register_special("\xee", "\xc3\xae");
    register_special("\xef", "\xc3\xaf");

    register_special("\xf0", "\xc3\xb0");
    register_special("\xf1", "\xc3\xb1");
    register_special("\xf2", "\xc3\xb2");
    register_special("\xf3", "\xc3\xb3");
    register_special("\xf4", "\xc3\xb4");
    register_special("\xf5", "\xc3\xb5");
    register_special("\xf6", "\xc3\xb6");
    register_special("\xf7", "\xc3\xb7");
    register_special("\xf8", "\xc3\xb8");
    register_special("\xf9", "\xc3\xb9");
    register_special("\xfa", "\xc3\xba");
    register_special("\xfb", "\xc3\xbb");
    register_special("\xfc", "\xc3\xbc");
    register_special("\xfd", "\xc3\xbd");
    register_special("\xfe", "\xc3\xbe");
    register_special("\xff", "\xc3\xbf");

    register_special("\xc2\xa6\xc3\x87", "\xc3\xa8");
    register_special("\xc2\xa6\xc3\xbc", "\xc3\x80");

    int in_spe = 0;
    char line[MAX_LINE_SIZE];
    char oline[MAX_LINE_SIZE];
    char* l = line, *ol = oline, *special;
    while ( 1 )
    {
	char c = getc(stdin);
	if ( current_line_in_spetree && ( c == 255 || c== 0xa ) )
	{
	    *l = 0;
	    strncpy(current_line_in_spetree, line, MAX_LINE_SIZE);
	    current_line_in_spetree = NULL;
	}
	if ( c == 255 )
	{
	    if ( in_spe == 2 )
	    {
		*l = 0;
		l = add_special(special, line);
	    }
	    break;
	}

        if ( c < 0x80 && in_spe == 2 )
	{
	    in_spe = 1;
	    *l = 0;
	    l = add_special(special, line);
	}

	*l++ = c;
	*ol++ = c;
	
	if ( c == 0xa )
	{
	    *l++ = 0;
	    *ol++ = 0;
	    if ( in_spe )
	    {
		if ( strncmp(oline, line, MAX_LINE_SIZE) )
		{
		    // fprintf(stdout, "< %s> %s", oline, line);
		}
		else
		{
		    fprintf(stdout, "| %s", line);
		}
	    }
	    in_spe = 0;
	    l = line;
	    ol = oline;
	    continue;
	}
	// fprintf(stdout, "Got %x %c\n", c, c);
	if ( c >= 0x80 )
	{
	    // fprintf(stdout, "[%x] %c", c, c);
	    switch ( in_spe )
	    {
		case 0:
		    in_spe = 2;
		    special = l - 1;
		    break;
		case 1:
		    special = l - 1;
		    in_spe = 2;
		    break;
	    }
	}
    }
    dump_all_specials();
}