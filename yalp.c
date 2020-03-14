#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

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
        const char* name;
        const char* message;
    };    
};

struct sexpr* new_sexpr(enum sexpr_t tag)
{
    struct sexpr* e = malloc(sizeof(struct sexpr));
    memset(e, 0, sizeof(struct sexpr));

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

struct symbol* get_env_symbol(struct env* env, const char* name)
{
    for(int i=0;i<env->symbol_count;i++)
    {
        if (strcmp(env->symbols[i]->name, name) == 0)
            return env->symbols[i];
    }

    return NULL;
}

void add_env_symbol(struct env* env, struct symbol* symbol)
{
    env->symbols = realloc(env->symbols, sizeof(struct symbol) * (env->symbol_count+1));
    env->symbol_count += 1;
    env->symbols[env->symbol_count-1] = symbol;
}

void add_env_function(struct env* env, const char* name, struct sexpr* (*fn) (struct env* env, struct symbol*, struct sexpr*))
{
    struct symbol* s = malloc(sizeof(struct symbol));
    s->tag = function;
    strcpy(s->name, name);
    s->function = fn;
    add_env_symbol(env, s);
}

void add_env_value(struct env* env, const char* name, struct sexpr* val)
{
    struct symbol* s = malloc(sizeof(struct symbol));
    s->tag = value;
    strcpy(s->name, name);
    s->value = val;
    add_env_symbol(env, s);    
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
        (c == '_');
}

bool is_operator(char c)
{   
    return         
        (c == '+') ||
        (c == '-') ||
        (c == '*') ||
        (c == '/');
}

struct sexpr* read_symbol(const char** str)
{
    if ((**str) == '\0')
        return NULL;

    if (is_digit(**str))
        return NULL; 

    char* name = malloc(MAX_SYMBOL_NAME);
    memset(name, '\0', MAX_SYMBOL_NAME);

    if (is_operator(**str))
    {
        name[0] = **str;
        (*str)++;
    }
    else
    {
        for (int i=0;i<MAX_SYMBOL_NAME-1;i++)
        {
            if (is_symbol_character(**str))
            {
                name[i] = **str;
                (*str)++;
            }
        }
    }

    struct sexpr* s = new_sexpr(symbol);
    s->name = name;

    return s;
}
struct sexpr* create_list(int element_count, ...)
{
    struct sexpr* head = NULL;
    struct sexpr* previous = NULL;
    va_list valist;
    va_start(valist, element_count); 

    for (int i=0;i<element_count;i++)
    {
        struct sexpr* element = va_arg(valist, struct sexpr*);
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = element;
        if (previous)
            previous->list.tail = cell;
        else
            head = cell;

        previous = cell;
    }

    va_end(valist); 
    
    return head;
}

struct sexpr* read_quote(const char** str)
{
    if (**str == '\'')
    {
        (*str)++;
        
        struct sexpr* s = new_sexpr(symbol);
        s->name = "quote";
        struct sexpr* sexpr = read_sexpr(str);
        return create_list(2, s, sexpr);        
    }

    return NULL;
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

    if ((e = read_quote(str)))
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

bool as_bool(struct sexpr* e)
{
    switch (e->tag)
    {
        case integer:
            return e->integer != 0;
        default:
            return false;
    }
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr);
struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n);

struct sexpr* eval_if(struct env* env, struct symbol* s, struct sexpr* args)
{
    struct sexpr* cond = eval_argument(env, args, 0);

    if (as_bool(cond))
        return eval_argument(env, args, 1);
    else
        return eval_argument(env, args, 2);
}

struct sexpr* eval_operator(struct env* env, struct symbol* s, struct sexpr* args)
{
    char op = s->name[0];

    int result = 0;
    if (op == '*' || op == '/')
        result = 1;
    bool first = true;
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
            case '/':
                if (args->list.tail && first)
                    result = value;
                else
                    result /= value;
                break;
        }
        args = args->list.tail;
        first = false;
    }

    struct sexpr* e = new_sexpr(integer);
    e->integer = result;
    return e;
}

struct sexpr* eval_quote(struct env* env, struct symbol* s, struct sexpr* args)
{
    return args->list.head; // Quote returns the unevaluated first argument
}

struct sexpr* eval_list(struct env* env, struct symbol* s, struct sexpr* args)
{
    if (!args)
        return NULL;
    struct sexpr* head = new_sexpr(list);
    struct sexpr* previous = NULL;

    while (args)
    {
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = eval_sexpr(env, args->list.head);
        cell->list.tail = NULL;
        if (previous)
            previous->list.tail = cell;
        else
            head = cell;
        previous = cell;
        args = args->list.tail;
    }

    return head;
}

struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n)
{
    struct sexpr* e = args;
    while (n-- > 0)
    {
        e = e->list.tail;
    }

    if (e)
        return eval_sexpr(env,e->list.head);
    else
        return NULL;
}

struct sexpr* eval_define(struct env* env, struct symbol* s, struct sexpr* args)
{
    struct sexpr* sym = args->list.head;

    CHECK_ERROR(sym);

    if (sym->tag != symbol)
    {
        return new_error("Argument not evaluated to symbol");
    }

    struct sexpr* value = eval_argument(env, args, 1);

    CHECK_ERROR(value);

    add_env_value(env, sym->name, value);

    return value;
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
        struct symbol* s = get_env_symbol(env, sexpr->list.head->name);

        if (!s)
        {
            printf("Undefined symbol: %s\n", sexpr->list.head->name);
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
    else if (sexpr && sexpr->tag == symbol)
    {
        struct symbol* s = get_env_symbol(env, sexpr->name);
        if (!s)
        {
            printf("Unknown symbol: %s\n", sexpr->name);
            return new_error("Unknown symbol");
        }
        else if(s->tag == value)
        {
            return s->value;
        }
        else
        {
            return sexpr; // TODO: Return function
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
            printf("%s", sexpr->name);
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
    add_env_function(env, "+", eval_operator);
    add_env_function(env, "-", eval_operator);
    add_env_function(env, "*", eval_operator);
    add_env_function(env, "/", eval_operator);
    add_env_function(env, "'", eval_quote);
    add_env_function(env, "quote", eval_quote);
    add_env_function(env, "list", eval_list);
    add_env_function(env, "define", eval_define);
    add_env_function(env, "if", eval_if);
}

void readline(char* buff, size_t size)
{
    memset(buff, '\0', size);
    for(int i=0;i<size-1;)
    {
        int data = getchar();
        if (data == EOF)
            return;
        if (data == '\r')
            continue;
        if (data == '\n')
            return;
        buff[i++] = data;
    }
}


int main()
{
    struct env env;
    set_env(&env);

    while (true)
    {
        char line[4096];
        printf("> "); readline(line, 4096);

        const char* input = line;

        struct sexpr* e = read_sexpr(&input);

        if (e->tag == error)
        {
            printf("Error: %s", e->message);
            continue;
        }

        e = eval_sexpr(&env, e);
        printf("< "); print_sexpr(e); printf("\n");
    }

    return 0;
}