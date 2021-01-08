
#include <sys/epoll.h>
#include <unistd.h>  // 在gcc编译器中，使用的头文件因gcc版本的不同而不同
#include <stdio.h>
#include <termio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

#define black 6
#define white 2
typedef int color;
char* BLACKBLOCK = "▐█";
char* WHITEBLOCK = "  ";
static struct termios old, current;

typedef struct _ROWCOL
{
	short r;
	short c;
} ROWCOL;

// 顺时针旋转
ROWCOL template[][4][4] = {
	// 正方形旋转也是一样的
	{
		{{0,0},{0,1},{-1,0},{-1,1}},
		{{0,0},{0,1},{-1,0},{-1,1}},
		{{0,0},{0,1},{-1,0},{-1,1}},
		{{0,0},{0,1},{-1,0},{-1,1}}
	},
	// 横/竖
	{
		{{0,0},{-1,0},{1,0},{2,0}},		// 竖
		{{0,0},{0,-1},{0,-2},{0,1}},	// 横
		{{0,0},{-1,0},{1,0},{2,0}},		// 竖
		{{0,0},{0,-1},{0,-2},{0,1}}		// 横
	},
	// 山
	{
		{{0,0},{0,-1},{0,1},{-1,0}},
		{{0,0},{0,1},{-1,0},{1,0}},
		{{0,0},{0,-1},{0,1},{1,0}},
		{{0,0},{0,-1},{-1,0},{1,0}}
	},
	// S
	{
		{{0,0},{0,-1},{-1,1},{-1,0}},
		{{0,0},{0,1},{-1,0},{1,1}},
		{{0,0},{1,-1},{0,1},{1,0}},
		{{0,0},{0,-1},{-1,-1},{1,0}}
	},
	// Z
	{
		{{0,0},{0,1},{-1,-1},{-1,0}},
		{{0,0},{0,1},{-1,1},{1,0}},
		{{0,0},{0,-1},{1,1},{1,0}},
		{{0,0},{0,-1},{-1,0},{1,-1}}
	},
	// J
	{
		{{0,0},{1,0},{1,-1},{-1,0}},
		{{0,0},{0,1},{-1,-1},{0,-1}},
		{{0,0},{-1,1},{1,0},{-1,0}},
		{{0,0},{0,1},{1,1},{0,-1}}
	},
	// L
	{
		{{0,0},{1,0},{1,1},{-1,0}},
		{{0,0},{0,1},{1,-1},{0,-1}},
		{{0,0},{-1,-1},{1,0},{-1,0}},
		{{0,0},{0,1},{-1,1},{0,-1}}
	}
};

// TODO游标可能越界
// 小于这个的是字符区域。ZERO_POS+1开始处是游标位
int ZERO_POS = 0;
int COL_SUM = 0;
int ROW_SUM = 0;

// 出生点
ROWCOL tetris/* = {2,10}*/;
int _type = 2;
int _rotate = 0;

ROWCOL reset[4] = {{0,0},{0,0},{0,0},{0,0}};
int reset_pos = 0;

int recycle[5] = { 0 };

/* Initialize new terminal i/o settings */
void initTermios(int echo) 
{
  tcgetattr(0, &old); /* grab old terminal i/o settings */
  current = old; /* make new settings same as old settings */
  current.c_lflag &= ~ICANON; /* disable buffered i/o */
  if (echo) {
      current.c_lflag |= ECHO; /* set echo mode */
  } else {
      current.c_lflag &= ~ECHO; /* set no echo mode */
  }
  tcsetattr(0, TCSANOW, &current); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios(void) 
{
  tcsetattr(0, TCSANOW, &old);
}

// 不同颜色
void SetColor(char** buffer, int r, int col, color c)
{
	if (c==black)
	{
		reset[reset_pos].r = r;
		reset[reset_pos].c = col;
		reset_pos++;
	}
	// TODO边界判断
	char* row = buffer[r];
	int step = c==white ? strlen(WHITEBLOCK) : strlen(BLACKBLOCK);
	
	int space = 0;
	int curPos = *(row+ZERO_POS+1+col);
	int nextPos = 0;
	if (col < COL_SUM-1)
	{
		nextPos = *(row+ZERO_POS+1+col+1);
		int _2or6 = nextPos - curPos;
		space = c - _2or6;	// 大于0往右移动，小于0往左移动
	}

	if (space != 0)
	{
		// 重叠区域要小心
		memmove(row + nextPos + space, row + nextPos, strlen(row + nextPos)+1/*结束符*/);
		
		// 修改索引
		for (int i = COL_SUM-1; i > col; --i)
		{
			*(row+ZERO_POS+1+i) += space;
		}
	}
	
	// 结束符复制的条件
	strncpy(row + curPos, c==white ? WHITEBLOCK : BLACKBLOCK, step+(space==0?1:0));
}

color GetColor(char** buffer, int r, int c)
{
	char* row = buffer[r];
	return ((c>=COL_SUM-1?strlen(row):*(row+ZERO_POS+1+c+1))-*(row+ZERO_POS+1+c))==strlen(WHITEBLOCK) ? white : black;
}

void Clear(char** buffer, int r, color c)
{
	char* row = buffer[r];
	int step = c==white ? strlen(WHITEBLOCK) : strlen(BLACKBLOCK);	// 2或者6
	int col;
	for (col = 0; col < 0 + COL_SUM; ++col)
	{
		strcpy(row + col * step, c==white ? WHITEBLOCK : BLACKBLOCK);
		*(row+ZERO_POS+1+col) = col * step;
	}
	// 相当于截断
	*(row + col * step) = '\0';
}

void ResetColor(char** buffer)
{
	for (int i = 0; i < reset_pos; ++i)
	{
		SetColor(buffer, reset[i].r, reset[i].c, white);
	}
	reset_pos = 0;
}

// 返回值1到底 0成功 -1无效
// 会和其他的块碰撞以及边界
int Move(char** buffer, ROWCOL move)
{
	ROWCOL* spec = template[_type][_rotate];
	for(int b = 0; b < 4; ++b)
	{
		int _r = tetris.r + spec[b].r + move.r;
		int _c = tetris.c + spec[b].c + move.c;
		if (_r >= ROW_SUM || _c < 0 || _c >= COL_SUM)
			return _r >= ROW_SUM ? 1 : -1;
		
		if (GetColor(buffer, _r, _c) != white)
			return move.r > 0 ? 1 : -1;
	}
	return 0;
}

// 返回值1到底 0成功 -1无效
int Rotate(char** buffer)
{
	int _ = (_rotate + 1) % 4;
	ROWCOL* spec = template[_type][_];
	for(int b = 0; b < 4; ++b)
	{
		int _r = tetris.r + spec[b].r;
		int _c = tetris.c + spec[b].c;
		if (_r >= ROW_SUM || _c < 0 || _c >= COL_SUM)
			return -1;
		
		if (GetColor(buffer, _r, _c) != white)
			return -1;
	}
	
	return 0;
}

void ShowTetris(char** buffer)
{
	ROWCOL* spec = template[_type][_rotate];
	for(int b = 0; b < 4; ++b)
	{
		SetColor(buffer, tetris.r + spec[b].r, tetris.c + spec[b].c, black);
	}
}

void Begin(char** buffer)
{
	tetris.r = 2;
	tetris.c = COL_SUM/2;

	int r = rand();
	_type = r % (sizeof(template) / sizeof(template[0]));
	_rotate = r % (sizeof(template[0]) / sizeof(template[0][0]));

	// 行回收
	for (int r = ROW_SUM - 1; r >= 0; r--)
	{
		int lineFull = 1;
		for (int c = 0; c < COL_SUM; c++)
		{
			if (GetColor(buffer, r, c) == white)
			{
				lineFull = 0;
				break;
			}
		}
		if (lineFull != 0)
			recycle[recycle[4]++] = r;
	}

	while (recycle[4] > 0)
	{
		recycle[4]--;
		int removeIndex = recycle[recycle[4]];
		char *temp = buffer[removeIndex];
		while (--removeIndex >= 0)
			buffer[removeIndex + 1] = buffer[removeIndex];

		buffer[0] = temp;
		Clear(buffer, 0, white);
	}
}

int main(int argc, char **argv)
{
	srand(time(0));// 随机种子
	
	int row = 32;
	int col = 20;
	if (argc > 1)
		row = atoi(argv[1]);
	if (argc > 2)
		col = atoi(argv[2]);
	
	tetris.c = col / 2;
	ZERO_POS = col * 6;
	COL_SUM = col;
	ROW_SUM = row;
	char* _line = "▔▔";
	char* line = (char**)malloc(sizeof(char*) * col * 6 + 1);
	for (int i = 0; i < col; ++i)
		sprintf(line + i * 6, "%s", _line);
	char** ROWBUFFER = (char**)malloc(sizeof(char*) * row);
	for(int r = 0; r < row; ++r)
	{
		// 一个黑块是两个小黑块组成。utf-8编码就是6个字符
		ROWBUFFER[r] = (char*)malloc(sizeof(char) * ZERO_POS + 1 + col);// 确保预留有结束符。后面再预留游标位
		memset(ROWBUFFER[r], 0, ZERO_POS + 1 + col);
		Clear(ROWBUFFER, r, /*r%2 == 0 ? black : */white);
	}
	
	int stdinput = 0;		// 标准输入流
	int epfd = epoll_create(1);	//生成epoll句柄
	struct epoll_event ev, events[1];
	ev.data.fd = stdinput;			//设置与要处理事件相关的文件描写叙述符
	ev.events = EPOLLIN;	//设置要处理的事件类型
	epoll_ctl(epfd, EPOLL_CTL_ADD, stdinput, &ev);//注册epoll事件
	
	int BEGIN_CODE = *((int*)"ltgo");
	int key = BEGIN_CODE;
	int sleepTime = -1;
	
	Begin(ROWBUFFER);
	initTermios(0);
	while(key != 'q' && key != 'Q'){
		if (epoll_wait(epfd,events,1,0) > 0)//等待事件发生
		{
			key = getchar();
			if (27 == key && getchar() == 91)
			{
				key = getchar();// 上下左右 65 66 68 67
			}
			
			// TODO优化读取多个命令
			fflush(stdin);
		}
		
		int ret = 0;
		if ((++sleepTime)%14 == 0)
		{
			if (key != 66)
			{
				ret = Move(ROWBUFFER, template[1][0][2]);
				if(ret == 0) tetris.r += 1;
			}
			
			// 旋转
			if (key == 65)
			{
				if (Rotate(ROWBUFFER) == 0)_rotate = (_rotate + 1) % 4;
			}
			
			if (key == 68 || key == 67)
			{
				ROWCOL offset = key == 68 ? template[1][1][1] : template[1][1][3];
				ret = Move(ROWBUFFER, offset);
				if (ret == 0) tetris.c += offset.c;
			}
			
			if (key != 66)
				key = BEGIN_CODE;
		}
		
		// 快速移动效果
		if (/*sleepTime%2 == 0 && */key == 66)
		{
			ret = Move(ROWBUFFER, template[1][0][2]);
			if(ret == 0) tetris.r += 1;
		}
			
		// 每秒33帧
		usleep(1000 * 30);
		// 调用system("clear")函数
		// printf("\x1b[H\x1b[2J")
		printf("\033c\n");	// 清理屏幕
		ShowTetris(ROWBUFFER);
		for(int r = 0; r < row; ++r)
		{
			printf(ROWBUFFER[r]);
			printf("|\n");
		}
		
		if (ret == 1){
			reset_pos = 0;
			key = BEGIN_CODE;
		//	Begin(ROWBUFFER);
		}
		printf("%s\n", line);
		ResetColor(ROWBUFFER);
		if (ret == 1)
			Begin(ROWBUFFER);
		
		// printf("████  ██ ██▐█▐█ ⬛⬜🔳🔳%d\n", (int)strlen("██  ██  ██"));
		// printf("████  ██ ██▐█   ⬛⬜🔳🔳 ▔-%d\n", (int)strlen("██  ██  ██"));
	}
	resetTermios();
	
	for(int r = 0; r < row; ++r)
	{
		free(ROWBUFFER[r]);
	}
	free(ROWBUFFER);
	return 0;
}
