#include <stdio.h>
#include <stdbool.h>

enum sexpr_t
{
    list,
    integer,
    operator
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
        char operator;
    };    
};

struct sexpr* new_sexpr(enum sexpr_t tag)
{
    struct sexpr* e = malloc(sizeof(struct sexpr));
    e->tag = tag;

    return e;
}

#define delete_sexpr free

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

struct sexpr* read_operator(const char** str)
{
    switch (**str)
    {
        case '+': case '-': case '*': case '/':
        {
            struct sexpr* e = new_sexpr(operator);
            e->operator = (*str)[0];
            (*str)++;
            return e;
        }
        default:
            return NULL;
    }
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

    if (e = read_integer(str))
        return e;
    else if (e = read_operator(str))
        return e;
    else if (e = read_list(str))
        return e;
    else
        return NULL; // TODO: ERROR
}

int as_integer(struct sexpr* e)
{
    if (e->tag == integer)
        return e->integer;
    else
        return 0; // Error?
}

struct sexpr* eval_sexpr(struct sexpr* sexpr);

struct sexpr* eval_operator(struct sexpr* list)
{
    char op = list->list.head->operator;

    int result = 0;
    if (op == '*' || op == '/')
        result = 1;

    list = list->list.tail;

    while (list)
    {
        int arg = as_integer(eval_sexpr(list->list.head));
        switch (op)
        {
            case '+': result += arg; break;
            case '-': result -= arg; break;
            case '*': result *= arg; break;
            case '/': result /= arg; break;
        }
        list = list->list.tail;
    }

    struct sexpr* e = new_sexpr(integer);
    e->integer = result;
    return e;
}

struct sexpr* eval_sexpr(struct sexpr* sexpr)
{
    // Only lists are evaluated
    if (sexpr && sexpr->tag == list)
    {
        return eval_operator(sexpr);
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
        case integer:
            printf("%d", sexpr->integer);
            break;
        case operator:
            printf("%c", sexpr->operator);
            break;
        case list:
            printf("(");
            while (sexpr)
            {
                print_sexpr(sexpr->list.head);
                
                if (sexpr = sexpr->list.tail)
                    printf(" ");
            }
            printf(")");
            break;
        }
    }

}

int main()
{
    const char* input = "(+ 17 (* 10 5 2) (- 5))";

    struct sexpr* e = read_sexpr(&input);
    print_sexpr(e); printf("\n");
    e = eval_sexpr(e);
    print_sexpr(e); printf("\n");

    return 0;
}