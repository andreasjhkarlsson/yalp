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
struct sexpr* read_sexpr(struct env* env, const char** str);
struct sexpr* create_list(struct env* env, int element_count,  ...);
struct sexpr* alloc_sexpr(struct env* env);

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

enum memory_mode_t
{
    untracked,
    tracked
};

struct sexpr
{
    enum memory_mode_t memory_mode;
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
    _NIL = { .memory_mode = untracked, .tag = nil },
    _S_TRUE = { .memory_mode = untracked, .tag = boolean, .boolean = true},
    _S_FALSE = { .memory_mode = untracked, .tag = boolean, .boolean = false};

struct sexpr* NIL = &_NIL;
struct sexpr* S_TRUE = &_S_TRUE;
struct sexpr* S_FALSE = &_S_FALSE;

struct sexpr* new_sexpr(struct env* env, enum sexpr_t tag)
{
    struct sexpr* e = alloc_sexpr(env);

    e->tag = tag;
    e->memory_mode = tracked;

    return e;
}

struct sexpr* new_integer(struct env* env, int n)
{
    struct sexpr* e = new_sexpr(env, integer);
    e->integer = n;
    return e;
}

struct sexpr* new_function(struct env* env, enum function_t tag)
{
    struct sexpr* e = new_sexpr(env, function);
    e->function.tag = tag;
    return e;
}

struct sexpr* new_symbol(struct env* env, const char* str, size_t length)
{
    struct sexpr* e = new_sexpr(env, symbol);
    e->name = malloc(length + 1);
    strncpy((char*)e->name, str, length);
    ((char*)e->name)[length] = '\0';
    return e;
}

struct sexpr* new_error(struct env* env, const char* message)
{
    struct sexpr* e = new_sexpr(env, error);
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
    struct sexpr* context;
};

struct frame* create_frame()
{
    struct frame* frame = malloc(sizeof(struct frame));
    frame->bindings = NULL;
    frame->binding_count = 0;
    frame->previous = NULL;
    frame->context = NULL;
    return frame;
}

void delete_binding(struct binding* binding) {
    free((void *)binding->name);
    binding->name = NULL;
    binding->value = NULL;
}

void remove_binding(struct frame* frame, const char* name, bool recursive)
{
    for(int i=0;i<frame->binding_count;i++)
    {
        struct binding* binding = &frame->bindings[i];
        if (binding->name && strcmp(binding->name, name) == 0)
        {
            delete_binding(binding);
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
            delete_binding(binding);
        } else if (binding->name == NULL)
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
            delete_binding(&frame->bindings[i]);
    }
    free(frame);
}

struct block
{
    struct sexpr sexpr;
    bool marked: 1;
    bool used: 1;
};

#define HEAP_SIZE 65536

struct env
{
    struct block heap[HEAP_SIZE];
    struct frame* stack;
};

size_t available_heap_space(struct env* env)
{
    size_t free_slots = 0;
    for (int i=0;i<HEAP_SIZE;i++)
    {
        if (!env->heap[i].used)
            free_slots++;
    }

    return free_slots;
}

struct sexpr* alloc_sexpr(struct env* env)
{
    for (int i=0; i<HEAP_SIZE;i++)
    {
        struct block* block = &env->heap[i];
        if (!block->used)
        {
            memset(block, 0, sizeof(struct block));
            block->used = true;
            return &block->sexpr;
        }
    }

    static struct sexpr memory_error = {.memory_mode = untracked, .tag = error};
    memory_error.message = "Out of memory";

    printf("Out of memory!\n");
    return &memory_error;
}

void mark_sexpr(struct sexpr* sexpr)
{
    if (sexpr->memory_mode == tracked)
    {
        struct block* block = (struct block*) sexpr;
        if (block->marked)
            return; 
        block->marked = true;
    }

    switch (sexpr->tag)
    {
        case list:
            mark_sexpr(sexpr->list.head);
            mark_sexpr(sexpr->list.tail);
            break;
        case function:
            if (sexpr->function.tag == lambda)
            {
                mark_sexpr(sexpr->function.lambda.params);
                mark_sexpr(sexpr->function.lambda.exprs);
            }
            break;
    }
}

void mark_frame(struct frame* frame)
{
    for (int i=0;i<frame->binding_count;i++)
    {
        struct binding* binding = &frame->bindings[i];
        if (binding->name && binding->value)
        {
            mark_sexpr(binding->value);
        }
    }
    // TODO: Mark context

    if (frame->previous)
        mark_frame(frame->previous);
}

void mark_roots(struct env* env)
{
    mark_frame(env->stack);
}

void sweep_heap(struct env* env)
{
    for (int i=0;i<HEAP_SIZE;i++)
    {
        struct block* block = &env->heap[i];

        if (!block->marked)
        {
            block->used = false;
        }

        block->marked = false;
    }
}

void collect_garbage(struct env* env)
{
    size_t available_before = available_heap_space(env);
    mark_roots(env);
    sweep_heap(env);
    size_t available_after = available_heap_space(env);

    printf("GC collected %d objects, heap now has %d slots available\n", available_after - available_before, available_after);
}

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
    struct sexpr* v = new_function(env, builtin);
    v->function.builtin.name = copy_string(name);
    v->function.builtin.fn = fn;

    add_env_binding(env, name, v);
}

void push_stack_frame(struct env* env, struct sexpr* context)
{
    struct frame* frame = create_frame();
    frame->previous = env->stack;
    frame->context = context;
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

struct sexpr* read_integer(struct env* env, const char** str)
{
    if (is_digit(**str))
    {
        int number = 0;
        while (is_digit(**str))
        {
            number = number * 10 + (**str) - '0';
            (*str)++;
        }
        struct sexpr* atom = new_sexpr(env, integer);
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

struct sexpr* read_operator(struct env* env, const char** str)
{
    if (is_operator(**str))
    {
        struct sexpr* s = new_symbol(env, *str, 1);
        (*str)++;
        return s;
    }

    return NULL;
}

struct sexpr* read_symbol(struct env* env, const char** str)
{
    struct sexpr* s;
    if ((s = read_operator(env, str)))
        return s;

    if (!is_symbol_character(**str) || is_digit(**str))
    {
        return NULL;
    }

    int count;
    for (count = 0;is_symbol_character((*str)[count]);count++);

    s = new_symbol(env, *str, count);
    (*str) += count;
    return s;
}
struct sexpr* create_list(struct env* env, int element_count, ...)
{
    struct sexpr* head = NIL;
    struct sexpr* previous = NULL;
    va_list valist;
    va_start(valist, element_count);

    for (int i=0;i<element_count;i++)
    {
        struct sexpr* element = va_arg(valist, struct sexpr*);
        struct sexpr* cell = new_sexpr(env, list);
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

struct sexpr* read_quote(struct env* env, const char** str)
{
    if (**str == '\'')
    {
        (*str)++;
        struct sexpr* s = new_sexpr(env, symbol);
        s->name = "quote";
        return create_list(env, 2, s, read_sexpr(env, str));
    }

    return NULL;
}

struct sexpr* read_list(struct env* env, const char** str)
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

            struct sexpr* cell = new_sexpr(env, list);
            cell->list.head = read_sexpr(env, str);
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

struct sexpr* read_sexpr(struct env* env, const char** str)
{
    struct sexpr* e = NULL;

    // Skip spaces
    skip_whitespace(str);

    if ((e = read_integer(env, str)))
        return e;

    if ((e = read_list(env, str)))
        return e;

    if ((e = read_quote(env, str)))
        return e;

    if ((e = read_boolean(str)))
        return e;

    if ((e = read_symbol(env, str)))
        return e;

    return new_error(env, "Syntax error");
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

    struct sexpr* sexpr = new_function(env, lambda);
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
        return new_error(env, "First argument to defun must be symbol");
    }

    struct sexpr* lambda = eval_lambda(env, args->list.tail);

    CHECK_ERROR(lambda);

    add_env_binding(env, s->name, lambda);

    return lambda;
}

struct sexpr* eval_if(struct env* env, struct sexpr* args)
{
    struct sexpr* cond = eval_argument(env, args, 0);

    size_t arg_count = list_length(args);

    if (as_bool(cond))
        return eval_argument(env, args, 1);
    else if (arg_count > 2)
        return eval_argument(env, args, 2);

    return NIL;
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
        state = eval_sexpr(env, create_list(env, 3, fn, el, state));

    return state;
}

struct sexpr* eval_bool_operator(struct env* env, struct sexpr* args, bool (*op) (struct sexpr*,struct sexpr*))
{
    int arg_length = list_length(args);
    if (arg_length < 2)
        return new_error(env, "At least 2 args are needed for binary operator");
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

bool equals(struct sexpr* left, struct sexpr* right)
{
    if (left == right)
        return true;
    return as_integer(left) == as_integer(right);
}

struct sexpr* eval_equals(struct env* env, struct sexpr* args)
{
    return eval_bool_operator(env, args, equals);
}

bool less(struct sexpr* left, struct sexpr* right)
{
    return as_integer(left) < as_integer(right);
}

struct sexpr* eval_less(struct env* env, struct sexpr* args)
{
    return eval_bool_operator(env, args, less);
}

struct sexpr* eval_int_operator(struct env* env, struct sexpr* args, int (*op) (int,int), int state)
{
    struct sexpr* result = new_integer(env, state);
    struct sexpr* arg;
    while ((arg = next(&args)))
    {
        arg = eval_sexpr(env, arg);
        CHECK_ERROR(arg);
        result->integer = op(result->integer, as_integer(arg));
    }

    return result;
}

int add(int a, int b) {
    return a + b;
}

struct sexpr* eval_add(struct env* env, struct sexpr* args)
{
    return eval_int_operator(env, args, add, 0);
}

int subtract(int a, int b) {
    return a - b;
}

struct sexpr* eval_subtract(struct env* env, struct sexpr* args)
{
    if (args == NIL || args->list.tail == NIL)
        return eval_int_operator(env, args, subtract, 0);
    else
    {
        struct sexpr* first = eval_argument(env, args, 0);
        CHECK_ERROR(first);
        return eval_int_operator(env, args->list.tail, subtract, as_integer(first));
    }

    return eval_int_operator(env, args, subtract, 0);
}

int multiply(int a, int b) {
    return a * b;
}

struct sexpr* eval_multiply(struct env* env, struct sexpr* args)
{
    return eval_int_operator(env, args, multiply, 1);
}

int divide(int a, int b) {
    return a / b;
}

struct sexpr* eval_division(struct env* env, struct sexpr* args)
{
    if (args == NIL || args->list.tail == NIL)
        return eval_int_operator(env, args, divide, 1);
    else
    {
        struct sexpr* first = eval_argument(env, args, 0);
        CHECK_ERROR(first);
        return eval_int_operator(env, args->list.tail, divide, as_integer(first));
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
        struct sexpr* cell = new_sexpr(env, list);
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
        return new_error(env, "Argument is of wrong type");
    return arg;
}


struct sexpr* eval_define(struct env* env, struct sexpr* args)
{
    struct sexpr* sym = args->list.head;

    CHECK_ERROR(sym);

    if (sym->tag != symbol)
    {
        return new_error(env, "Argument not evaluated to symbol");
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

    push_stack_frame(env, lambda);

    // Iterate over and bind parameters
    struct sexpr* param;
    int pos = 0;
    while ((param = next(&params)))
    {
        struct sexpr* arg = eval_argument(env, args, pos);

        if (!arg)
        {
            pop_stack_frame(env);
            return new_error(env, "Not enough arguments for lambda function");
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


struct sexpr* eval_loop(struct env* env, struct sexpr* args)
{
    int arg_count = list_length(args);

    if (arg_count < 3)
        return new_error(env, "loop needs at least 3 arguments");

    struct sexpr* params = next(&args);

    struct sexpr* initial_args = next(&args);

    struct sexpr* body = args;

    struct sexpr* l = new_function(env, lambda); 

    l->function.lambda.params = params;
    l->function.lambda.exprs = body;

    return call_lambda(env, l, initial_args);
}

struct sexpr* eval_recur(struct env* env, struct sexpr* args)
{
    struct sexpr* lambda = env->stack->context;

    if (!lambda)
        return new_error(env, "recur can only be used inside of lambda");

    return call_lambda(env, lambda, args);
}

struct sexpr* eval_progn(struct env* env, struct sexpr* args)
{
    struct sexpr* result = NIL;

    struct sexpr* arg;
    while ((arg = next(&args)))
    {
        result = eval_sexpr(env, arg);
    }

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
            return new_error(env, "Non function value found when evaluating list");
        }
    }
    else if (sexpr->tag == symbol)
    {
        struct sexpr* value = get_env_binding(env, sexpr->name);
        if (!value)
        {
            printf("Unknown symbol: %s\n", sexpr->name);
            return new_error(env, "Unknown symbol");
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
    memset(env->heap, 0, sizeof(env->heap));
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
    add_env_builtin_function(env, "recur", eval_recur);
    add_env_builtin_function(env, "loop", eval_loop);
    add_env_builtin_function(env, "progn", eval_progn);
}

void readline(char* buff, size_t size, bool* eof)
{
    memset(buff, '\0', size);
    for(size_t i=0;i<size-1;)
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
    size_t total_bytes;
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

        struct sexpr* e = read_sexpr(&env, &input);

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

        collect_garbage(&env);
    }

    free_string_builder(&input_builder);

    return 0;
}