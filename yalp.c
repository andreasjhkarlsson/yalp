#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

enum sexpr_t
{
    error,
    list,
    integer,
    symbol
};

struct sexpr
{
    enum sexpr_t tag;
    union 
    {
        struct
        {
            struct sexpr* head;
            struct sexpr* tail;
        } list;
        int integer;
        const char* symbol;
        const char* message;
    };    
};

struct sexpr* new_sexpr(enum sexpr_t tag)
{
    struct sexpr* e = malloc(sizeof(struct sexpr));
    e->tag = tag;

    return e;
}

struct sexpr* new_error(const char* message)
{
    struct sexpr* e = new_sexpr(error);
    e->message = message;
    return e;
}

#define CHECK_ERROR(expr) if (expr && expr->tag == error) return expr;

#define MAX_SYMBOL_NAME 64

enum symbol_type
{
    function,
    value
};

struct env;

struct symbol
{
    char name [MAX_SYMBOL_NAME];
    enum symbol_type tag;
    union 
    {
        struct sexpr* (*function) (struct env* env, struct symbol*, struct sexpr*);
        struct sexpr* value;
    };
};

struct env
{
    struct symbol** symbols;
    int symbol_count;
};

struct symbol* get_symbol(struct env* env, const char* name)
{
    for(int i=0;i<env->symbol_count;i++)
    {
        if (strcmp(env->symbols[i]->name, name) == 0)
            return env->symbols[i];
    }

    return NULL;
}

void add_symbol(struct env* env, struct symbol* symbol)
{
    env->symbols = realloc(env->symbols, env->symbol_count+1);
    env->symbol_count += 1;
    env->symbols[env->symbol_count-1] = symbol;
}

void add_function(struct env* env, const char* name, struct sexpr* (*fn) (struct env* env, struct symbol*, struct sexpr*))
{
    struct symbol* s = malloc(sizeof(struct symbol));
    s->tag = function;
    strcpy(s->name, name);
    s->function = fn;
    add_symbol(env, s);
}

struct sexpr* read_sexpr(const char** str);

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

struct sexpr* read_integer(const char** str)
{
    if (is_digit(**str))
    {
        int number = 0;
        while (is_digit(**str))
        {
            number = number * 10 + (**str) - '0';
            (*str)++;
        }
        struct sexpr* atom = new_sexpr(integer);
        atom->integer = number;
        return atom;
    }
    else
    {
        return NULL;
    }
}

bool is_symbol_character(char c)
{
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == '_') ||
        (c == '+') ||
        (c == '-') ||
        (c == '*') ||
        (c == '/') ||
        (c == '\'');
}

struct sexpr* read_symbol(const char** str)
{
    if ((*str) == '\0')
        return NULL;

    if (is_digit(**str))
        return NULL; 

    char* name = malloc(MAX_SYMBOL_NAME);
    memset(name, '\0', MAX_SYMBOL_NAME);

    for (int i=0;i<MAX_SYMBOL_NAME-1;i++)
    {
        if (is_symbol_character(**str))
        {
            name[i] = **str;
            (*str)++;
        }
    }

    struct sexpr* e = new_sexpr(symbol);
    e->symbol = name;

    return e;
}

struct sexpr* read_list(const char** str)
{
    if ((**str) == '(')
    {
        (*str)++;
        struct sexpr* head = NULL;
        struct sexpr* previous = NULL;
        
        while ((*str)[0] != ')')
        {
            if ((*str)[0] == ' ')
            {
                (*str)++;
                continue;
            }
            struct sexpr* cell = new_sexpr(list);
            cell->list.head = read_sexpr(str);
            cell->list.tail = NULL;

            if (previous)
                previous->list.tail = cell;
            else
                head = cell;                

            previous = cell;
        }
        (*str)++;

        return head;
    }
    else
    {
        return NULL;
    }
    
}

struct sexpr* read_sexpr(const char** str)
{
    struct sexpr* e = NULL;

    // Skip spaces
    if ((**str) == ' ')
    {
        (*str)++;
        return read_sexpr(str);
    }

    if ((e = read_integer(str)))
        return e;

    if ((e = read_list(str)))
        return e;

    if ((e = read_symbol(str)))
        return e;

    return new_error("Syntax error");
}

int as_integer(struct sexpr* e)
{
    if (e->tag == integer)
        return e->integer;
    else
        return 0; // Error?
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr);

struct sexpr* eval_operator(struct env* env, struct symbol* s, struct sexpr* args)
{
    char op = s->name[0];

    int result = 0;
    if (op == '*' || op == '/')
        result = 1;

    while (args)
    {
        struct sexpr* arg = eval_sexpr(env, args->list.head);
        CHECK_ERROR(arg);

        int value = as_integer(arg);
        switch (op)
        {
            case '+': result += value; break;
            case '-': result -= value; break;
            case '*': result *= value; break;
            case '/': result /= value; break;
        }
        args = args->list.tail;
    }

    struct sexpr* e = new_sexpr(integer);
    e->integer = result;
    return e;
}

struct sexpr* eval_quote(struct env* env, struct symbol* s, struct sexpr* args)
{
    return args->list.head; // Quote returns the unevaluated first argument
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr)
{
    // Only lists are evaluated
    if (sexpr && sexpr->tag == list)
    {
        if (sexpr->list.head->tag != symbol)
        {
            return new_error("Encountered non symbol in list evaluation");
        }
        struct symbol* s = get_symbol(env, sexpr->list.head->symbol);

        if (!s)
        {
            printf("Undefined symbol: %s\n", sexpr->list.head->symbol);
            return new_error("Undefined symbol in expression");
        }
        
        if (s->tag == function)
        {
            struct sexpr* args = sexpr->list.tail;
            return s->function(env, s, args);
        }
        else
        {
            return s->value;
        }
    }
    else
    {
        return sexpr;
    }
}

void print_sexpr(struct sexpr* sexpr)
{
    if (sexpr == NULL)
    {
        printf("()");
    }
    else
    {
        switch (sexpr->tag)
        {
        case error:
            printf("Error: %s", sexpr->message);
            break;
        case integer:
            printf("%d", sexpr->integer);
            break;
        case symbol:
            printf("%s", sexpr->symbol);
            break;
        case list:
            printf("(");
            while (sexpr)
            {
                print_sexpr(sexpr->list.head);
                
                if ((sexpr = sexpr->list.tail))
                    printf(" ");
            }
            printf(")");
            break;
        }
    }
}

void set_env(struct env* env)
{
    env->symbols = NULL;
    env->symbol_count = 0;
    add_function(env, "+", eval_operator);
    add_function(env, "-", eval_operator);
    add_function(env, "*", eval_operator);
    add_function(env, "/", eval_operator);
    add_function(env, "'", eval_quote);
    add_function(env, "quote", eval_quote);
}

int main()
{
    struct env env;
    set_env(&env);

    const char* input = "(/ 5 2)";

    struct sexpr* e = read_sexpr(&input);
    print_sexpr(e); printf("\n");
    e = eval_sexpr(&env, e);
    print_sexpr(e); printf("\n");

    return 0;
}