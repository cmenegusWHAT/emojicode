//
//  Lexer.c
//  Emojicode
//
//  Created by Theo Weidmann on 28.02.15.
//  Copyright (c) 2015 Theo Weidmann. All rights reserved.
//

#include <cstring>
#include "Lexer.hpp"
#include "utf8.h"
#include "CompilerErrorException.hpp"

#define isNewline() (c == 0x0A || c == 0x2028 || c == 0x2029)

bool detectWhitespace(EmojicodeChar c, size_t *col, size_t *line) {
    if (isNewline()) {
        *col = 0;
        (*line)++;
        return true;
    }
    return isWhitespace(c);
}

TokenStream lex(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f || ferror(f)) {
        throw CompilerErrorException(SourcePosition(0, 0, path), "Couldn't read input file %s.", path);
    }
    
    const char *dot = strrchr(path, '.');
    if (!dot || strcmp(dot, ".emojic")) {
        throw CompilerErrorException(SourcePosition(0, 0, path), "Emojicode files must be suffixed with .emojic: %s", path);
    }
    
    EmojicodeChar c;
    size_t i = 0;
    
    auto sourcePosition = SourcePosition(1, 0, path);
    
    Token *token = new Token(sourcePosition);
    TokenStream stream = TokenStream(token);
    token = new Token(sourcePosition, token);
    
    bool nextToken = false;
    bool oneLineComment = false;
    bool isHex = false;
    bool escapeSequence = false;
    
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    auto stringBuffer = new(std::nothrow) char[length + 1];
    if (stringBuffer) {
        fread(stringBuffer, 1, length, f);
        stringBuffer[length] = 0;
    }
    else {
        throw CompilerErrorException(sourcePosition, "Cannot allocate buffer for file %s. It is possibly to large.", path);
    }
    
#define isIdentifier() ((0x1F300 <= c && c <= 0x1F64F) || (0x1F680 <= c && c <= 0x1F6C5) || (0x1F6CB <= c && c <= 0x1F6F3) || (0x2600 <= c && c <= 0x27BF) || (0x1F191 <= c && c <= 0x1F19A) || c == 0x231A || (0x1F910 <= c && c <= 0x1F9C0) || (0x2B00 <= c && c <= 0x2BFF) || (0x25A0 <= c && c <= 0x25FF) || (0x2300 <= c && c <= 0x23FF) || (0x2190 <= c && c <= 0x21FF))
    
    while (i < length) {
        size_t delta = i;
        c = u8_nextchar(stringBuffer, &i);
        sourcePosition.character += i - delta;
        
        if (!nextToken) {
            switch (token->type()) {
                case COMMENT:
                    if (!oneLineComment) {
                        detectWhitespace(c, &sourcePosition.character, &sourcePosition.line);
                        if (c == E_OLDER_WOMAN) {
                            /* End of the comment, reset the token for future use */
                            token->type_ = NO_TYPE;
                        }
                    }
                    else {
                        detectWhitespace(c, &sourcePosition.character, &sourcePosition.line);
                        if (isNewline()) {
                            token->type_ = NO_TYPE;
                        }
                    }
                    continue;
                case DOCUMENTATION_COMMENT:
                    detectWhitespace(c, &sourcePosition.character, &sourcePosition.line);
                    if (c == E_TACO) {
                        nextToken = true;
                    }
                    else {
                        token->value.push_back(c);
                    }
                    continue;
                case STRING:
                    if (escapeSequence) {
                        switch (c) {
                            case E_INPUT_SYMBOL_LATIN_LETTERS:
                            case E_CROSS_MARK:
                                token->value.push_back(c);
                                break;
                            case 'n':
                                token->value.push_back('\n');
                                break;
                            case 't':
                                token->value.push_back('\t');
                                break;
                            case 'r':
                                token->value.push_back('\r');
                                break;
                            case 'e':
                                token->value.push_back('\e');
                                break;
                            default: {
                                ecCharToCharStack(c, tc);
                                throw CompilerErrorException(sourcePosition, "Unrecognized escape sequence ❌%s.", tc);
                            }
                        }
                        
                        escapeSequence = false;
                    }
                    else if (c == E_INPUT_SYMBOL_LATIN_LETTERS) {
                        /* End of string, time for a new token */
                        nextToken = true;
                    }
                    else if (c == E_CROSS_MARK) {
                        escapeSequence = true;
                    }
                    else {
                        detectWhitespace(c, &sourcePosition.character, &sourcePosition.line);
                        token->value.push_back(c);
                    }
                    continue;
                case VARIABLE:
                    if (detectWhitespace(c, &sourcePosition.character, &sourcePosition.line)) {
                        /* End of variable */
                        nextToken = true;
                        continue;
                    }
                    else if (isIdentifier() || c == 0x3017 || c == 0x3016) {
                        /* End of variable */
                        nextToken = true;
                    }
                    else {
                        token->value.push_back(c);
                        continue;
                    }
                    break;
                case INTEGER:
                    if ((47 < c && c < 58) || (((64 < c && c < 71) || (96 < c && c < 103)) && isHex)) {
                        token->value.push_back(c);
                        continue;
                    }
                    else if (c == 46) {
                        /* A period. Seems to be a float */
                        token->type_ = DOUBLE;
                        token->value.push_back(c);
                        continue;
                    }
                    else if ((c == 'x' || c == 'X') && token->value.size() == 1 && token->value[0] == '0') {
                        isHex = true;
                        token->value.push_back(c);
                        continue;
                    }
                    else if (c == '_') {
                        continue;
                    }
                    else {
                        token->validateInteger(isHex);
                        // An unexpected character, seems to be a new token
                        nextToken = true;
                    }
                    break;
                case DOUBLE:
                    // Note: 46 is not required, we are just lexing the floating numbers
                    if ((47 < c && c < 58)) {
                        token->value.push_back(c);
                        continue;
                    }
                    else {
                        token->validateDouble();
                        // An unexpected character, seems to be a new token
                        nextToken = true;
                    }
                    break;
                case SYMBOL:
                    token->value.push_back(c);
                    nextToken = true;
                    continue;
                default:
                    break;
            }
        }

        if (detectWhitespace(c, &sourcePosition.character, &sourcePosition.line)) {
            continue;
        }
        if (nextToken) {
            token = new Token(sourcePosition, token);
            nextToken = false;
        }
        
        if (c == E_INPUT_SYMBOL_LATIN_LETTERS) {
            token->type_ = STRING;
        }
        else if (c == E_OLDER_WOMAN || c == E_OLDER_MAN) {
            token->type_ = COMMENT;
            oneLineComment = (c == E_OLDER_MAN);
        }
        else if (c == E_TACO) {
            token->type_ = DOCUMENTATION_COMMENT;
        }
        else if ((47 < c && c < 58) || c == 45 || c == 43) {
            token->type_ = INTEGER;
            token->value.push_back(c);
            
            isHex = false;
        }
        else if (c == E_THUMBS_UP_SIGN || c == E_THUMBS_DOWN_SIGN) {
            token->type_ = (c == E_THUMBS_UP_SIGN) ? BOOLEAN_TRUE : BOOLEAN_FALSE;
            nextToken = 1;
        }
        else if (c == E_KEYCAP_10) {
            token->type_ = SYMBOL;
        }
        else if (c == 0x3016) {  // 〖
            token->type_ = ARGUMENT_BRACKET_OPEN;
            nextToken = true;
        }
        else if (c == 0x3017) {  // 〗
            token->type_ = ARGUMENT_BRACKET_CLOSE;
            nextToken = true;
        }
        else if (isIdentifier()) {
            token->type_ = IDENTIFIER;
            token->value.push_back(c);
            nextToken = true;
        }
        else {
            token->type_ = VARIABLE;
            token->value.push_back(c);
        }
    }
    delete [] stringBuffer;
    
    if (!nextToken && token->type() == STRING) {
        throw CompilerErrorException(token->position(), "Expected 🔤 but found end of file instead.");
    }
    if (!nextToken && token->type() == COMMENT && !oneLineComment) {
        throw CompilerErrorException(token->position(), "Expected 👵 but found end of file instead.");
    }
    if (token->type() == INTEGER) {
        token->validateInteger(isHex);
    }
    else if (token->type() == DOUBLE) {
        token->validateDouble();
    }

#undef isIdentifier
    fclose(f);
    return stream;
}
