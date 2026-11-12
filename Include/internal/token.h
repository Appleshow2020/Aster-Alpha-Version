/* 자동 생성: Tools/build/generate_token.py (원본: Grammar/Tokens). 직접 고치지 마세요. */
#ifndef ASTER_TOKEN_H
#define ASTER_TOKEN_H

enum TokenType {
    TOK_ENDMARKER,
    TOK_NAME,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_LPAR,
    TOK_RPAR,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LSQB,
    TOK_RSQB,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_AT,
    TOK_ARROW,
    TOK_FATARROW,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_DOUBLESLASH,
    TOK_PERCENT,
    TOK_DOUBLESTAR,
    TOK_EQEQUAL,
    TOK_NOTEQUAL,
    TOK_LESS,
    TOK_GREATER,
    TOK_LESSEQUAL,
    TOK_GREATEREQUAL,
    TOK_EQUAL,
    TOK_PLUSEQUAL,
    TOK_MINEQUAL,
    TOK_STAREQUAL,
    TOK_SLASHEQUAL,
    TOK_DOUBLESLASHEQUAL,
    TOK_PERCENTEQUAL,
    TOK_DOUBLESTAREQUAL,
    TOK_DOTDOT,
    TOK_PIPE,
    TOK_VBAR,
    TOK_DOUBLEQUESTION,
    TOK_QUESTIONDOT,
    TOK_QUESTION,
    TOK_AMPER,
    TOK_N_TOKENS
};

typedef struct { const char *text; int type; } ExactToken;

extern const char *const _AsterToken_Names[];
extern const ExactToken _AsterToken_Exact[];
extern const int _AsterToken_ExactCount;
#define ASTER_MAX_OPERATOR_LENGTH 3

#endif
