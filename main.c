#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define READ_INDEX	0
#define WRITE_INDEX 1

#define BADFD -1
typedef enum
{
	T_WORD,
	T_BAR,
	T_AMP,
	T_SEMI,
	T_GT,
	T_GTGT,
	T_LT,
	T_NL,
	T_EOF
} TOKEN;

enum
{
	NUETRAL,
	GTGT,
	INQUOTE,
	INWORD
};

static void redirect(int   srcfd,
					 char *srcfile,
					 int   dstfd,
					 char *dstfile,
					 bool  append,
					 bool  bckgrnd)
{
	int flags, fd;

	if (srcfd == 0 && bckgrnd)
	{
		strcpy(srcfile, "/dev/null");
		srcfd = BADFD;
	}
	if (srcfd != 0)
	{
		if (srcfd > 0)
		{
			dup2(srcfd, 0);
		} else if (open(srcfile, O_RDONLY, 0) != -1)
		{
			fprintf(stderr, "CAnt open %s\n", srcfile);
			exit(0);
		}
	}
	if (dstfd != 1)
	{
		close(1);
		if (dstfd > 1)
		{
			dup2(dstfd, 1);
		} else
		{
			flags = O_WRONLY | O_CREAT;
			if (!append)
				flags |= O_TRUNC;
			if (open(dstfile, flags, 0666) == -1)
			{
				fprintf(stderr, "CAnt create %s", dstfile);
				exit(0);
			}
			if (append)
				lseek(1, 0L, 2);
		}
	}
	for (fd = 3; fd < 20; fd++)
		close(fd);
}

bool isbuiltin()
{
	return false;
}
static int invoke(int	 argc,
				  char **argv,
				  int	 srcfd,
				  char  *srcfile,
				  int	 dstfd,
				  char  *dstfile,
				  bool	 append,
				  bool	 bckgrnd)
{
	int pid;

	if (argc == 0 || isbuiltin())
		return (0);
	switch (pid = fork())
	{
	case -1:
		fprintf(stderr, "Cant create new process\n");
		break;
	case 0:
		redirect(srcfd, srcfile, dstfd, dstfile, append, bckgrnd);
		execvp(argv[0], argv);
		fprintf(stderr, "Cant execurte %s\n", argv[0]);
		exit(0);
	default:
		if (srcfd > 0)
			close(srcfd);
		if (dstfd > 1)
			close(dstfd);
		if (bckgrnd)
			printf("%d\n", pid);
	}
	return (pid);
}

static int gettoken(char *word)
{
	int	  state = NUETRAL;
	int	  c;
	char *w;

	w = word;
	while ((c = getchar()) != EOF)
	{
		switch (state)
		{
		case NUETRAL:
			switch (c)
			{
			case ';':
				return (T_SEMI);
			case '&':
				return (T_AMP);
			case '|':
				return (T_BAR);
			case '<':
				return (T_LT);
			case '\n':
				return (T_NL);
			case ' ':
			case '\t':
				continue;
			case '>':
				state = GTGT;
				continue;
			case '"':
				state = INQUOTE;
				continue;
			default:
				state = INWORD;
				*w++  = c;
				continue;
			}
		case GTGT:
			if (c == '>')
				return (T_GTGT);
			ungetc(c, stdin);
			return (T_GT);
		case INQUOTE:
			switch (c)
			{
			case '\\':
				*w++ = getchar();
				continue;
			case '"':
				*w = '\0';
				return (T_WORD);
			default:
				*w++ = c;
				continue;
			}
		case INWORD:
			switch (c)
			{

			case ';':
			case '&':
			case '|':
			case '<':
			case '>':
			case '\n':
			case ' ':
			case '\t':
				ungetc(c, stdin);
				*w = '\0';
				return (T_WORD);
			default:
				*w++ = c;
				continue;
			}
		}
	}
	return (T_EOF);
}

#define MAXARG		20
#define MAXFILENAME 200
#define MAXWORD		200
static int command(int *waitpid, bool makepipe, int *pipefdp)
{
	TOKEN token, term;
	int	  argc, srcfd, dstfd, pid, pfd[2];
	char *argv[MAXARG + 1], srcfile[MAXFILENAME], dstfile[MAXFILENAME];
	char  word[MAXWORD];
	bool  append;

	argc  = 0;
	srcfd = 0;
	dstfd = 1;
	while (1)
	{
		switch (token = gettoken(word))
		{
		case T_WORD:
			if (argc == MAXARG)
			{
				fprintf(stderr, "too many arguments\n");
				break;
			}
			if ((argv[argc] = malloc(strlen(word) + 1)) == NULL)
			{
				fprintf(stderr, "Out of arg memory\n");
				break;
			}
			strcpy(argv[argc], word);
			argc++;
			continue;
		case T_LT:
			if (makepipe)
			{
				fprintf(stderr, "extra <\n");
				break;
			}
			if (gettoken(srcfile) != T_WORD)
			{
				fprintf(stderr, "illegal <\n");
				break;
			}
			srcfd = BADFD;
			continue;
		case T_GT:
		case T_GTGT:
			if (dstfd != 1)
			{
				fprintf(stderr, "extra > or >>\n");
				break;
			}
			if (gettoken(dstfile) != T_WORD)
			{
				fprintf(stderr, "illegal <\n");
				break;
			}
			dstfd  = BADFD;
			append = (token == T_GTGT);
			continue;
		case T_BAR:
		case T_AMP:
		case T_SEMI:
		case T_NL:
			argv[argc] = NULL;
			if (token == T_BAR)
			{
				if (dstfd != 1)
				{
					fprintf(stderr, "> or >> conflicts with |\n");
					break;
				}
				term = command(waitpid, true, &dstfd);
			} else
				term = token;
			if (makepipe)
			{
				if (pipe(pfd) == -1)
					perror("pipe");
				*pipefdp = pfd[WRITE_INDEX];
				srcfd	 = pfd[READ_INDEX];
			}
			pid = invoke(argc,
						 argv,
						 srcfd,
						 srcfile,
						 dstfd,
						 dstfile,
						 append,
						 term == T_AMP);
			for (int i = 0; i < argc; i++)
				printf("%d:%s \n", i, argv[i]);
			if (token != T_BAR)
				*waitpid = pid;
			if (argc == 0 && (token != T_NL || srcfd > 1))
				fprintf(stderr, "missing command\n");
			while (--argc >= 0)
				free(argv[argc]);
			return (term);
		case T_EOF:
			exit(0);
		}
	}
}

int main()
{
	char *prompt;
	int	  pid, fd, status;
	TOKEN term;

	prompt = "> ";
	printf("%s", prompt);
	while (1)
	{
		term = command(&pid, false, NULL);
		if (term != T_AMP && pid != 0)
			waitpid(pid, &status, 0);
		if (term == T_NL)
			printf("%s", prompt);
		for (fd = 3; fd < 20; fd++)
			close(fd);
	}
}

/*
//main for getttoken
int main(int argc, char **argv)
{
	char word[200];

	while (1)
	{
		switch (gettoken(word))
		{
		case T_WORD:
			printf("T_WORD : %s\n", word);
			break;
		case T_BAR:
			printf("T_BAR\n");
			break;
		case T_AMP:
			printf("T_AMP\n");
			break;
		case T_SEMI:
			printf("T_SEMI\n");
			break;
		case T_GT:
			printf("T_GT\n");
			break;
		case T_GTGT:
			printf("T_GTGT\n");
			break;
		case T_LT:
			printf("T_LT\n");
			break;
		case T_NL:
			printf("T_NL\n");
			break;
		case T_EOF:
			printf("T_EOF\n");
			exit(0);
		}
	}
}
*/