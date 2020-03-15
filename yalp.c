#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

enum sexpr_t;
struct sexpr;
struct env;
struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr);
struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n);
struct sexpr* eval_type_argument(struct env* env, struct sexpr* args, int n, enum sexpr_t type);
struct sexpr* call_lambda(struct env* env, struct sexpr* lambda, struct sexpr* args);
struct sexpr* read_sexpr(const char** str);
struct sexpr* create_list(int element_count, ...);

enum sexpr_t
{
    nil,
    error,
    list,
    integer,
    symbol,
    function,
    boolean
};

enum function_t
{
    builtin,
    lambda
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
        bool boolean;
        const char* name;
        const char* message;
        struct
        {
            enum function_t tag;
            union
            {
                struct
                {
                    const char* name;
                    struct sexpr* (*fn) (struct env*, struct sexpr*);
                } builtin;
                struct 
                {
                    struct sexpr* params;
                    struct sexpr* exprs;
                } lambda;
            };
        } function;
    };    
}
    _NIL = { .tag = nil },
    _S_TRUE = { .tag = boolean, .boolean = true},
    _S_FALSE = { .tag = boolean, .boolean = false};

struct sexpr* NIL = &_NIL;
struct sexpr* S_TRUE = &_S_TRUE;
struct sexpr* S_FALSE = &_S_FALSE;

struct sexpr* new_sexpr(enum sexpr_t tag)
{
    struct sexpr* e = malloc(sizeof(struct sexpr));
    memset(e, 0, sizeof(struct sexpr));

    e->tag = tag;

    return e;
}

struct sexpr* new_integer(int n)
{
    struct sexpr* e = new_sexpr(integer);
    e->integer = n;
    return e;
}

struct sexpr* new_function(enum function_t tag)
{
    struct sexpr* e = new_sexpr(function);
    e->function.tag = tag;
    return e;
}

struct sexpr* new_symbol(const char* str, size_t length)
{
    struct sexpr* e = new_sexpr(symbol);
    e->name = malloc(length + 1);
    strncpy((char*)e->name, str, length);
    ((char*)e->name)[length] = '\0';
    return e;
} 

struct sexpr* new_error(const char* message)
{
    struct sexpr* e = new_sexpr(error);
    e->message = message;
    return e;
}

const char* copy_string(const char* str)
{
    char* copy = malloc(strlen(str) + 1);
    strcpy(copy, str);
    return copy;
}

#define CHECK_ERROR(expr) if (!expr || expr->tag == error) return expr;

struct binding
{
    const char* name;
    struct sexpr* value;
};

struct frame
{
    struct binding* bindings;
    int binding_count;
    struct frame* previous;
};

struct frame* create_frame()
{
    struct frame* frame = malloc(sizeof(struct frame));
    frame->bindings = NULL;
    frame->binding_count = 0;
    frame->previous = NULL;
    return frame;
}

void remove_binding(struct frame* frame, const char* name, bool recursive)
{
    for(int i=0;i<frame->binding_count;i++)
    {
        struct binding* binding = &frame->bindings[i];
        if (binding->name && strcmp(binding->name, name) == 0)
        {
            free(binding->name);
            binding->name = NULL;
            binding->value = NULL;
            return;
        }
    }

    if (frame->previous && recursive)
        remove_binding(frame->previous, name, recursive);
}

void add_binding(struct frame* frame, const char* name, struct sexpr* value)
{
    // Try reusing a free spot
    for (int i=0;i<frame->binding_count;i++)
    {
        struct binding* binding = &frame->bindings[i];
        if (binding->name && strcmp(binding->name, name) == 0)
        {
            free(binding->name);
            binding->name = NULL;
            binding->value = NULL;
        }
        if (binding->name == NULL)
        {
            binding->name = copy_string(name);
            binding->value = value;
            return;
        }
    }

    frame->bindings = realloc(frame->bindings, sizeof(struct binding) * (frame->binding_count+1));
    frame->binding_count += 1;
    frame->bindings[frame->binding_count-1].name = copy_string(name);
    frame->bindings[frame->binding_count-1].value = value;
}

struct sexpr* get_binding(struct frame* frame, const char* name)
{
    for(int i=0;i<frame->binding_count;i++)
    {
        if (frame->bindings[i].name && strcmp(frame->bindings[i].name, name) == 0)
            return frame->bindings[i].value;
    }

    if (frame->previous)
        return get_binding(frame->previous, name);

    return NULL;
}

void free_frame(struct frame* frame)
{
    for (int i=0;i<frame->binding_count;i++)
    {
        if (frame->bindings[i].name)
            free(frame->bindings[i].name);
    }
    free(frame);
}

struct env
{
    struct frame* stack;
};

struct sexpr* get_env_binding(struct env* env, const char* name)
{
    return get_binding(env->stack, name);
}


void add_env_binding(struct env* env, const char* name, struct sexpr* val)
{
    add_binding(env->stack, name, val);
}

void remove_env_binding(struct env* env, const char* name, bool recursive)
{
    remove_binding(env->stack, name, recursive);
}

void add_env_builtin_function(struct env* env, const char* name, struct sexpr* (*fn) (struct env* env, struct sexpr*))
{
    struct sexpr* v = new_function(builtin);
    v->function.builtin.name = copy_string(name);
    v->function.builtin.fn = fn;

    add_env_binding(env, name, v);
}

void push_stack_frame(struct env* env)
{
    struct frame* frame = create_frame();
    frame->previous = env->stack;
    env->stack = frame;
}

void pop_stack_frame(struct env* env)
{
    struct frame* new_top = env->stack->previous;
    free_frame(env->stack);
    env->stack = new_top;
}

struct sexpr* next(struct sexpr** list)
{
    if (*list == NIL)
        return NULL;

    struct sexpr* element = (*list)->list.head;
    *list =(*list)->list.tail;
    return element;
}

int list_length(struct sexpr* list)
{
    int count = 0;
    while (next(&list)) count++;
    return count;
}

struct sexpr* reduce(struct sexpr* (*fn) (struct sexpr*, struct sexpr*), struct sexpr* list, struct sexpr* state)
{
    struct sexpr* el;
    while ((el = next(&list)))
        state = fn(el, state);
    return state;
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_whitespace(char c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

void skip_whitespace(const char** str)
{
    for (;(**str) != '\0' && is_whitespace(**str); (*str)++);
}

bool is_prefix(const char* prefix, const char* str)
{
    return strncmp(prefix, str, strlen(prefix)) == 0;
}

struct sexpr* read_boolean(const char** str)
{
    if (is_prefix("true", *str))
    {
        (*str) += 4;
        return S_TRUE;
    }
    else if (is_prefix("false", *str))
    {
        (*str) += 5;
        return S_FALSE;
    }
    
    return false;
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
    switch(c)
    {
        case '+':
        case '-':
        case '*':
        case '/':
        case '=':
        case '<':
            return true;
        default:
            return false;
    }
}

struct sexpr* read_operator(const char** str)
{
    if (is_operator(**str))
    {
        struct sexpr* s = new_symbol(*str, 1);
        (*str)++;
        return s;
    }

    return NULL;    
}

struct sexpr* read_symbol(const char** str)
{
    struct sexpr* s;
    if (s = read_operator(str))
        return s;

    if (!is_symbol_character(**str) || is_digit(**str))
    {
        return NULL;
    }

    int count;
    for (count = 0;is_symbol_character((*str)[count]);count++);

    s = new_symbol(*str, count);
    (*str) += count;
    return s;
}
struct sexpr* create_list(int element_count, ...)
{
    struct sexpr* head = NIL;
    struct sexpr* previous = NULL;
    va_list valist;
    va_start(valist, element_count); 

    for (int i=0;i<element_count;i++)
    {
        struct sexpr* element = va_arg(valist, struct sexpr*);
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = element;
        cell->list.tail = NIL;
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
        return create_list(2, s, read_sexpr(str));
    }

    return NULL;
}

struct sexpr* read_list(const char** str)
{
    if ((**str) == '(')
    {
        (*str)++;
        struct sexpr* head = NIL;
        struct sexpr* previous = NULL;
        
        while (true)
        {
            skip_whitespace(str);
            if ((*str)[0] == ')')
                break;

            struct sexpr* cell = new_sexpr(list);
            cell->list.head = read_sexpr(str);
            cell->list.tail = NIL;

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
    skip_whitespace(str);

    if ((e = read_integer(str)))
        return e;

    if ((e = read_list(str)))
        return e;

    if ((e = read_quote(str)))
        return e;

    if ((e = read_boolean(str)))
        return e;

    if ((e = read_symbol(str)))
        return e;

    return new_error("Syntax error");
}

int as_integer(struct sexpr* e)
{
    if (e->tag == integer)
        return e->integer;
    else if (e->tag == boolean)
        return e->boolean ? 1: 0;
    else
        return 0; // Error?
}

bool as_bool(struct sexpr* e)
{
    switch (e->tag)
    {
        case boolean:
            return e->boolean;
        case integer:
            return e->integer != 0;
        default:
            return false;
    }
}


struct sexpr* eval_lambda(struct env* env, struct sexpr* args)
{
    struct sexpr* params = args->list.head;
    struct sexpr* body = args->list.tail;

    struct sexpr* sexpr = new_function(lambda);
    sexpr->function.lambda.params = params;
    sexpr->function.lambda.exprs = body;

    return sexpr;
}

struct sexpr* eval_defun(struct env* env, struct sexpr* args)
{
    struct sexpr* s = args->list.head;

    CHECK_ERROR(s);

    if (s->tag != symbol)
    {
        return new_error("First argument to defun must be symbol");
    }

    struct sexpr* lambda = eval_lambda(env, args->list.tail);

    CHECK_ERROR(lambda);

    add_env_binding(env, s->name, lambda);

    return lambda;
}

struct sexpr* eval_if(struct env* env, struct sexpr* args)
{
    struct sexpr* cond = eval_argument(env, args, 0);

    if (as_bool(cond))
        return eval_argument(env, args, 1);
    else
        return eval_argument(env, args, 2);
}

struct sexpr* eval_reduce(struct env* env, struct sexpr* args)
{
    struct sexpr* fn = eval_type_argument(env, args, 0, function);
    CHECK_ERROR(fn);

    struct sexpr* lst = eval_type_argument(env, args, 1, list);
    CHECK_ERROR(lst);

    struct sexpr* state = eval_argument(env, args, 2);
    CHECK_ERROR(state);

    struct sexpr* el;
    while ((el = next(&lst)))
        state = eval_sexpr(env, create_list(3, fn, el, state));    
    
    return state;
}

struct sexpr* eval_bool_operator(struct env* env, struct sexpr* args, bool (*op) (struct sexpr*,struct sexpr*))
{
    int arg_length = list_length(args);
    if (arg_length < 2)
        return new_error("At least 2 args are needed for binary operator");
    bool result = true;

    struct sexpr* left = next(&args);
    struct sexpr* right;
    while ((right = next(&args)))
    {
        struct sexpr* eleft = eval_sexpr(env, left);
        CHECK_ERROR(eleft);

        struct sexpr* eright = eval_sexpr(env, right);
        CHECK_ERROR(eright);

        result = result && op(eleft, eright);

        left = right;
    }

    return result ? S_TRUE: S_FALSE;
}

struct sexpr* eval_equals(struct env* env, struct sexpr* args)
{
    bool equals(struct sexpr* left, struct sexpr* right)
    {
        if (left == right)
            return true;
        return as_integer(left) == as_integer(right);
    }

    return eval_bool_operator(env, args, equals);
}

struct sexpr* eval_less(struct env* env, struct sexpr* args)
{
    bool less(struct sexpr* left, struct sexpr* right)
    {
        return as_integer(left) < as_integer(right);
    }

    return eval_bool_operator(env, args, less);
}

struct sexpr* eval_int_operator(struct env* env, struct sexpr* args, int (*op) (int,int), int state)
{
    struct sexpr* result = new_integer(state);
    struct sexpr* arg;
    while ((arg = next(&args)))
    {
        arg = eval_sexpr(env, arg);
        CHECK_ERROR(arg);
        result->integer = op(result->integer, as_integer(arg));
    }

    return result;
}

struct sexpr* eval_add(struct env* env, struct sexpr* args)
{
    int op(int a, int b) { return a + b; }
    return eval_int_operator(env, args, op, 0);
}

struct sexpr* eval_subtract(struct env* env, struct sexpr* args)
{
    int op(int a, int b) { return a - b; }
    
    if (args == NIL || args->list.tail == NIL)
        return eval_int_operator(env, args, op, 0);
    else
    {
        struct sexpr* first = eval_argument(env, args, 0);
        CHECK_ERROR(first);
        return eval_int_operator(env, args->list.tail, op, as_integer(first));
    }

    return eval_int_operator(env, args, op, 0);
}

struct sexpr* eval_multiply(struct env* env, struct sexpr* args)
{
    int op(int a, int b) { return a * b; }
    return eval_int_operator(env, args, op, 1);
}

struct sexpr* eval_division(struct env* env, struct sexpr* args)
{
    int op(int a, int b) { return a / b; }
    if (args == NIL || args->list.tail == NIL)
        return eval_int_operator(env, args, op, 1);
    else
    {
        struct sexpr* first = eval_argument(env, args, 0);
        CHECK_ERROR(first);
        return eval_int_operator(env, args->list.tail, op, as_integer(first));
    }
}

struct sexpr* eval_quote(struct env* env, struct sexpr* args)
{
    return args->list.head; // Quote returns the unevaluated first argument
}

struct sexpr* eval_list(struct env* env, struct sexpr* args)
{
    struct sexpr* head = NIL;
    struct sexpr* previous = NULL;

    struct sexpr* el;
    while ((el = next(&args)))
    {
        struct sexpr* cell = new_sexpr(list);
        cell->list.head = eval_sexpr(env, el);
        cell->list.tail = NIL;
        if (previous)
            previous->list.tail = cell;
        else
            head = cell;
        previous = cell;
    }

    return head;
}

struct sexpr* eval_argument(struct env* env, struct sexpr* args, int n)
{
    while (n-- > 0)
        next(&args);

    if (args != NIL)
        return eval_sexpr(env,args->list.head);
    else
        return NIL;
}

struct sexpr* eval_type_argument(struct env* env, struct sexpr* args, int n, enum sexpr_t type)
{
    struct sexpr* arg = eval_argument(env, args, n);
    CHECK_ERROR(arg);
    if (arg->tag != type)
        return new_error("Argument is of wrong type");
    return arg;
}


struct sexpr* eval_define(struct env* env, struct sexpr* args)
{
    struct sexpr* sym = args->list.head;

    CHECK_ERROR(sym);

    if (sym->tag != symbol)
    {
        return new_error("Argument not evaluated to symbol");
    }

    struct sexpr* value = eval_argument(env, args, 1);

    CHECK_ERROR(value);

    add_env_binding(env, sym->name, value);

    return value;
}

struct sexpr* call_lambda(struct env* env, struct sexpr* lambda, struct sexpr* args)
{
    struct sexpr* params = lambda->function.lambda.params;
    struct sexpr* body = lambda->function.lambda.exprs;

    push_stack_frame(env);

    // Iterate over and bind parameters
    struct sexpr* param;
    int pos = 0;
    while (param = next(&params))
    {
        struct sexpr* arg = eval_argument(env, args, pos);

        if (!arg)
        {
            pop_stack_frame(env);
            return new_error("Not enough arguments for lambda function");
        }

        add_env_binding(env, param->name, arg);
        pos++;
    }

    struct sexpr* result = NULL;
    struct sexpr* expr;
    while ((expr = next(&body)))
        result = eval_sexpr(env, expr);

    // Popping stack frame also clears bindings
    pop_stack_frame(env);

    CHECK_ERROR(result);

    return result;
}

struct sexpr* eval_sexpr(struct env* env, struct sexpr* sexpr)
{
    // Only lists are evaluated
    if (sexpr->tag == list)
    {
        struct sexpr* value = eval_sexpr(env, sexpr->list.head);
        
        if (value->tag == function)
        {
            struct sexpr* args = sexpr->list.tail;
            switch (value->function.tag)
            {
                case builtin:
                    return value->function.builtin.fn(env, args);
                case lambda:
                    return call_lambda(env, value, args);
            }
        }
        else
        {
            return new_error("Non function value found when evaluating list");
        }
    }
    else if (sexpr->tag == symbol)
    {
        struct sexpr* value = get_env_binding(env, sexpr->name);
        if (!value)
        {
            printf("Unknown symbol: %s\n", sexpr->name);
            return new_error("Unknown symbol");
        }
        else
        {
            return value;
        }
    }
    else
    {
        return sexpr;
    }
}

void print_sexpr(struct sexpr* sexpr)
{
    switch (sexpr->tag)
    {
    case error:
        printf("Error: %s", sexpr->message);
        break;
    case nil:
        printf("()");
        break;
    case integer:
        printf("%d", sexpr->integer);
        break;
    case symbol:
        printf("%s", sexpr->name);
        break;
    case boolean:
        if (sexpr->boolean)
            printf("true");
        else
            printf("false");
        break;
    case function:
        switch (sexpr->function.tag)
        {
            case builtin:
                printf("<builtin function '%s'>", sexpr->function.builtin.name);
                break;
            case lambda:
                printf("<lambda function>");
                break;
        }
        break;
    case list:
        printf("(");
        while (sexpr != NIL)
        {
            print_sexpr(sexpr->list.head);
            
            if ((sexpr = sexpr->list.tail) != NIL)
                printf(" ");
        }
        printf(")");
        break;
    }
}

struct sexpr* eval_print(struct env* env, struct sexpr* args)
{
    struct sexpr* arg;
    while ((arg = next(&args)))
    {
        struct sexpr* value = eval_sexpr(env, arg);
        CHECK_ERROR(value);
        print_sexpr(value);
    }

    return NIL;
}

struct sexpr* eval_printl(struct env* env, struct sexpr* args)
{
    eval_print(env, args);

    printf("\n");

    return NIL;    
}

void set_env(struct env* env)
{
    env->stack = create_frame();
    add_env_builtin_function(env, "+", eval_add);
    add_env_builtin_function(env, "-", eval_subtract);
    add_env_builtin_function(env, "*", eval_multiply);
    add_env_builtin_function(env, "/", eval_division);
    add_env_builtin_function(env, "=", eval_equals);
    add_env_builtin_function(env, "<", eval_less);
    add_env_builtin_function(env, "'", eval_quote);
    add_env_builtin_function(env, "quote", eval_quote);
    add_env_builtin_function(env, "list", eval_list);
    add_env_builtin_function(env, "define", eval_define);
    add_env_builtin_function(env, "if", eval_if);
    add_env_builtin_function(env, "lambda", eval_lambda);
    add_env_builtin_function(env, "defun", eval_defun);
    add_env_builtin_function(env, "reduce", eval_reduce);
    add_env_builtin_function(env, "print", eval_print);
    add_env_builtin_function(env, "printl", eval_printl);
}

void readline(char* buff, size_t size, bool* eof)
{
    memset(buff, '\0', size);
    for(int i=0;i<size-1;)
    {
        int data = getchar();
        if (data == EOF)
        {
            if (eof)
                *eof = true;
            return;
        }
        if (data == '\r')
            continue;
        if (data == '\n')
            return;
        buff[i++] = data;
    }
}

int paren_balance(const char* str)
{
    int balance = 0;
    char c;
    while((c = *(str++)) != '\0')
    {
        if (c == '(')
            balance++;
        else if (c == ')')
            balance--;
    }

    return balance;
}

struct string_builder
{
    char* str;
    int total_bytes;
};

void init_string_builder(struct string_builder *builder)
{
    static const size_t DEFAULT_SIZE = 64;
    builder->str = (char*) malloc(DEFAULT_SIZE);
    builder->total_bytes = DEFAULT_SIZE;
    builder->str[0] = '\0';
}

void append_string_builder(struct string_builder* builder, const char* str)
{
    size_t current_length = strlen(builder->str);
    size_t new_length = current_length + strlen(str) + 1;
    if (new_length > builder->total_bytes)
    {
        builder->total_bytes *= 2;
        builder->str = realloc(builder->str, builder->total_bytes);        
    }

    strncat(builder->str, str, new_length - current_length);
}

void reset_string_builder(struct string_builder* builder)
{
    builder->str[0] = '\0';
}

void free_string_builder(struct string_builder* builder)
{
    free(builder->str);
}


int main()
{
    struct env env;
    set_env(&env);

    struct string_builder input_builder;
    init_string_builder(&input_builder);
    
    bool eof = false;

    while (!eof)
    {
        static char buffer[4096];
        printf("> ");
        
        reset_string_builder(&input_builder);
        do
        {
            readline(buffer, 4096, &eof);
            const char* line = buffer;
        
            skip_whitespace(&line);

            append_string_builder(&input_builder, line);
            append_string_builder(&input_builder, "\n");
        } while(!eof && paren_balance(input_builder.str) > 0);

        const char* input = input_builder.str;

        if (input[0] == '\0')
            continue;

        struct sexpr* e = read_sexpr(&input);

        skip_whitespace(&input);

        if (input[0] != '\0')
        {
            printf("Error: unparsed content in string: %s\n", input);
            continue;
        }

        if (!e || e->tag == error)
        {
            printf("Error: %s\n", e->message);
            continue;
        }

        e = eval_sexpr(&env, e);
        printf("< "); print_sexpr(e); printf("\n");
    }

    free_string_builder(&input_builder);

    return 0;
}