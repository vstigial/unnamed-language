#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUTFILE "out.asm"
#define MAX_WORD_LENGTH 1024
#define TOKENS_BASE_CAPACITY 1024

char **lex(const char *buffer, size_t *token_count);
void parse(char **tokens, size_t count, FILE *out);

typedef struct {
  char **strings;
  size_t count;
  size_t capacity;
} StringTable;

void escape_string(char *str) {
  size_t len = strlen(str);
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '\\' && i + 1 < len) {
      if (str[i + 1] == 'n') {
        str[i] = 0xa;
        memmove(str + i + 1, str + i + 2, len - i - 1);
        len--;
      }
    }
  }
}

void add_string(StringTable *table, const char *str) {
  char *str_copy = strdup(str);
  escape_string(str_copy);

  for (size_t i = 0; i < table->count; i++) {
    if (strcmp(table->strings[i], str_copy) == 0) {
      return; // String already added
    }
  }

  if (table->count >= table->capacity) {
    table->capacity = table->capacity ? table->capacity * 2 : 4;
    table->strings = realloc(table->strings, table->capacity * sizeof(char *));
  }

  table->strings[table->count++] = str_copy;
}

void output_string_table(FILE *out, StringTable *string_table) {
  for (size_t i = 0; i < string_table->count; i++) {
    fprintf(out, "string_%zu: db ", i);
    for (size_t x = 1; x < strlen(string_table->strings[i]) - 1; x++) {
      if (string_table->strings[i][x] == 0xa) {
        fprintf(out, "0xa, ");
      } else {
        fprintf(out, "0x%x, ", (unsigned char)string_table->strings[i][x]);
      }
    }
    fprintf(out, "0x0\n");
  }
}

bool print_int_called = false;
StringTable string_table = {0};

typedef struct {
  size_t start_label;
  size_t end_label;
  char type; // 'I' for if, 'W' for while
} ControlFlowContext;

ControlFlowContext control_stack[256];
int control_stack_top = -1;
size_t label_counter = 0;

typedef struct {
  char *name;
  char **content;
  size_t content_count;
} Macro;

Macro macros[1024];
size_t macro_count = 0;

char *externs[1024];
size_t extern_count = 0;

void add_macro(const char *name, char **content, size_t content_count) {
  // redefinitions are supported
  size_t index = macro_count;
  bool redef = false;
  for (size_t i = 0; i < macro_count; i++) {
    if (strcmp(macros[i].name, name) == 0) {
      printf("Warning: redefining macro: %s", name);
      index = i;
      redef = true;
    }
  }

  macros[index].name = strdup(name);
  macros[index].content = content;
  macros[index].content_count = content_count;
  if (!redef)
    macro_count++;
}

int parse_macro(const char *name, FILE *out, char **tokens) {
  for (size_t i = 0; i < macro_count; i++) {
    if (strcmp(macros[i].name, name) == 0) {
      parse(macros[i].content, macros[i].content_count, out);
      return 0;
    }
  }
  return 1;
}

bool err_flag = false;
void parse(char **tokens, size_t count, FILE *out) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(tokens[i], "macro") == 0) {
      i++;
      char *macro_name = tokens[i];
      char **macro_content = malloc(TOKENS_BASE_CAPACITY * sizeof(char *));
      size_t macro_content_count = 0;
      i++;
      while (i < count && strcmp(tokens[i], "endmacro") != 0) {
        macro_content[macro_content_count++] = tokens[i++];
      }
      add_macro(macro_name, macro_content, macro_content_count);
    } else if (strcmp(tokens[i], "push") == 0) {
      if (tokens[++i][0] == '"') {
        add_string(&string_table, tokens[i]);
        fprintf(out, " mov rax, string_%zu\n push rax\n push %zu\n",
                string_table.count - 1,
                strlen(string_table.strings[string_table.count - 1]) - 2);
      } else {
        fprintf(out, " push %s\n", tokens[i]);
      }
    } else if (strcmp(tokens[i], "include") == 0) {
      char *file = tokens[++i];
      memmove(&file[0], &file[1], strlen(file));
      memmove(&file[strlen(file) - 1], &file[strlen(file)], 1);
      FILE *included_file = fopen(file, "r");
      if (!included_file) {
        printf("Error opening included file: \"%s\": ", tokens[i]);
        perror("");
        exit(1);
      }
      fseek(included_file, 0, SEEK_END);
      size_t file_size = ftell(included_file);
      fseek(included_file, 0, SEEK_SET);
      char *buffer = malloc(file_size + 1);
      size_t j = 0;
      char c;
      while ((c = fgetc(included_file)) != EOF) {
        buffer[j++] = c;
      }
      buffer[j] = '\0';
      fclose(included_file);
      size_t inc_count;
      char **inc_tokens = lex(buffer, &inc_count);
      free(buffer);
      parse(inc_tokens, inc_count, out);
    } else if (strcmp(tokens[i], "extern") == 0) {
      i++;
      bool prev_add = false;
      for (size_t j = 0; j < extern_count; j++) {
        if (strcmp(externs[j], tokens[i]) == 0) {
          // already added
          prev_add = true;
        }
      }
      if (!prev_add)
        externs[extern_count++] = tokens[i];

      /* External Calls */
    } else if (strcmp(tokens[i], "call0") == 0) {
      fprintf(out, " xor rax, rax\n call %s\n push rax\n", tokens[++i]);

    } else if (strcmp(tokens[i], "pop") == 0) {
      fprintf(out, " pop %s\n", tokens[++i]);
    } else if (strcmp(tokens[i], "push64") == 0) {
      fprintf(out, " mov rax, %s\n push rax\n", tokens[++i]);
    } else if (strcmp(tokens[i], "add") == 0) {
      fprintf(out, " pop rax\n pop rbx\n add rax, rbx\n push rax\n");
    } else if (strcmp(tokens[i], "sub") == 0) {
      fprintf(out, " pop rbx\n pop rax\n sub rax, rbx\n push rax\n");
    } else if (strcmp(tokens[i], "mul") == 0) {
      fprintf(out, " pop rax\n pop rbx\n mul rbx\n push rax\n");
    } else if (strcmp(tokens[i], "shl") == 0) {
      fprintf(out, " pop rcx\n pop rbx\n shl rbx, cl\n push rbx\n");
    } else if (strcmp(tokens[i], "shr") == 0) {
      fprintf(out, " pop rcx\n pop rbx\n shr rbx, cl\n push rbx\n");
    } else if (strcmp(tokens[i], "sal") == 0) {
      fprintf(out, " pop rcx\n pop rbx\n sal rbx, cl\n push rbx\n");
    } else if (strcmp(tokens[i], "sar") == 0) {
      fprintf(out, " pop rcx\n pop rbx\n sar rbx, cl\n push rbx\n");
    } else if (strcmp(tokens[i], "or") == 0) {
      fprintf(out, " pop rax\n pop rbx\n or rbx, rax\n push rbx\n");
    } else if (strcmp(tokens[i], "and") == 0) {
      fprintf(out, " pop rax\n pop rbx\n and rbx, rax\n push rbx\n");
    } else if (strcmp(tokens[i], "not") == 0) {
      fprintf(out, " pop rax\n not rax\n push rax\n");
    } else if (strcmp(tokens[i], "exit") == 0) {
      fprintf(out, " pop rcx\n call ExitProcess\n");
    } else if (strcmp(tokens[i], "print") == 0) {
      fprintf(out, " call print_integer\n");
      print_int_called = true;
    } else if (strcmp(tokens[i], "drop") == 0) {
      fprintf(out, " pop rax\n");
    } else if (strcmp(tokens[i], "puts") == 0) {
      fprintf(out, " mov rcx, 4294967285\n call GetStdHandle\n"
                   " mov rcx, rax\n pop r8\n pop rdx\n xor r9, r9\n call "
                   "WriteConsoleA\n");
    } else if (strcmp(tokens[i], "wputs") == 0) {
      fprintf(out, " mov rcx, 4294967285\n call GetStdHandle\n"
                   " mov rcx, rax\n pop r8\n pop rdx\n xor r9, r9\n call "
                   "WriteConsoleW\n");
    } else if (strcmp(tokens[i], "dup") == 0) {
      fprintf(out, " pop rax\n push rax\n push rax\n");
    } else if (strcmp(tokens[i], "over") == 0) {
      fprintf(out, " pop rax\n pop rbx\n push rbx\n push rax\n push rbx\n");
    } else if (strcmp(tokens[i], "rot") == 0) {
      fprintf(
          out,
          " pop rax\n pop rbx\n pop rcx\n push rbx\n push rax\n push rcx\n");
    } else if (strcmp(tokens[i], "swap") == 0) {
      fprintf(out, " pop rax\n pop rbx\n push rax\n push rbx\n");
    } else if (strcmp(tokens[i], "2swap") == 0) {
      fprintf(out, " pop rax\n pop rbx\n pop rcx\n pop rdx\n push rbx\n push "
                   "rax\n push rdx\n push rcx\n");
    } else if (strcmp(tokens[i], "argc") == 0) {
      fprintf(out, " mov rax, [rel argc]\n push rax\n");
    } else if (strcmp(tokens[i], "argv") == 0) {
      fprintf(out, " lea rax, [rel argv]\n push rax\n");

      /* MEMORY */
    } else if (strcmp(tokens[i], "HeapAlloc") == 0) {
      fprintf(out, " sub rsp, 32\n mov rcx, [rsp+32]\n mov rdx, [rsp+40]\n mov "
                   "r8, [rsp+48]\n call HeapAlloc\n add rsp, 32\n pop rbx\n "
                   "pop rbx\n pop rbx\n push rax\n");
    } else if (strcmp(tokens[i], "GetProcessHeap") == 0) {
      fprintf(out,
              " sub rsp, 32\n call GetProcessHeap\n add rsp, 32\n push rax\n");
    } else if (strcmp(tokens[i], "HeapSet") == 0) {
      fprintf(out, " pop rax\n pop rbx\n pop rcx\n mov [rax + rbx], rcx\n");
    } else if (strcmp(tokens[i], "HeapGet") == 0) {
      fprintf(out, " pop rax\n pop rbx\n mov rcx, [rax + rbx]\n push rcx\n");
    } else if (strcmp(tokens[i], "HeapFree") == 0) {
      fprintf(out, " pop rcx\n pop rdx\n pop r8\n call HeapFree\n push rax\n");
    } else if (strcmp(tokens[i], "HeapReAlloc") == 0) {
      fprintf(out, " pop rcx\n pop rdx\n pop r8\n pop r9\n call HeapReAlloc\n "
                   "push rax\n");

    } else if (strcmp(tokens[i], "store8") == 0) {
      fprintf(out, " pop rbx\n pop rax\n mov [rax], bl\n");
    } else if (strcmp(tokens[i], "load8") == 0) {
      fprintf(out, " pop rbx\n xor rax, rax\n mov al, [rbx]\n push rax\n");
    } else if (strcmp(tokens[i], "store16") == 0) {
      fprintf(out, " pop rbx\n pop rax\n mov [rax], bx\n");
    } else if (strcmp(tokens[i], "load16") == 0) {
      fprintf(out, " pop rbx\n xor rax, rax\n mov ax, [rbx]\n push rax\n");
    } else if (strcmp(tokens[i], "store32") == 0) {
      fprintf(out, " pop rbx\n pop rax\n mov [rax], ebx\n");
    } else if (strcmp(tokens[i], "load32") == 0) {
      fprintf(out, " pop rbx\n xor rax, rax\n mov eax, [rbx]\n push rax\n");
    } else if (strcmp(tokens[i], "store64") == 0) {
      fprintf(out, " pop rbx\n pop rax\n mov [rax], rbx\n");
    } else if (strcmp(tokens[i], "load64") == 0) {
      fprintf(out, " pop rbx\n xor rax, rax\n mov rax, [rbx]\n push rax\n");

      /* CONTROL FLOW */
    } else if (strcmp(tokens[i], "if") == 0) {
      // pass
    } else if (strcmp(tokens[i], "then") == 0) {
      size_t false_label = label_counter++;
      fprintf(out, " pop rax\n test rax, rax\n jz label_%zu\n", false_label);
      control_stack[++control_stack_top] = (ControlFlowContext){
          .start_label = 0, .end_label = false_label, .type = 'I'};
    } else if (strcmp(tokens[i], "else") == 0) {
      size_t end_label = label_counter++;
      fprintf(out, " jmp label_%zu\n", end_label);
      fprintf(out, "label_%zu:\n", control_stack[control_stack_top].end_label);
      control_stack[control_stack_top].end_label = end_label;
    } else if (strcmp(tokens[i], "end") == 0) {
      if (control_stack[control_stack_top].type == 'W') {
        fprintf(out, " jmp label_%zu\n",
                control_stack[control_stack_top].start_label);
        fprintf(out, "label_%zu:\n",
                control_stack[control_stack_top].end_label);
      } else if (control_stack[control_stack_top].type == 'I') {
        fprintf(out, "label_%zu:\n",
                control_stack[control_stack_top].end_label);
      }
      control_stack_top--;
    } else if (strcmp(tokens[i], "do") == 0) {
      fprintf(out, " pop rax\n test rax, rax\n jz label_%zu\n",
              control_stack[control_stack_top].end_label);
    } else if (strcmp(tokens[i], "while") == 0) {
      size_t start_label = label_counter++;
      size_t end_label = label_counter++;
      fprintf(out, "label_%zu:\n", start_label);
      control_stack[++control_stack_top] = (ControlFlowContext){
          .start_label = start_label, .end_label = end_label, .type = 'W'};
    } else if (strcmp(tokens[i], "==") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmovne rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], "!=") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmove rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], ">") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmovg rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], "<") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmovl rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], ">=") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmovge rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], "<=") == 0) {
      fprintf(out, " mov rcx, 1\n mov rdx, 0\n pop rax\n pop rbx\n cmp rax, "
                   "rbx\n cmovle rcx, rdx\n push rcx\n");
    } else if (strcmp(tokens[i], "") == 0) {
      // TODO: files that end with comments will return empty tokens
      continue;
    } else {
      if (parse_macro(tokens[i], out, tokens) == 1) {
        // TODO: instead of printing i, print the line and column of the error
        // (create a token struct with char *token, size_t column, size_t line)
        printf("Error: unknown token `%s` at %d\n", tokens[i], i);
        err_flag = true;
      }
    }
    // free(tokens[i]);
  }
}

char **lex(const char *buffer, size_t *token_count) {
  char **tokens = malloc(TOKENS_BASE_CAPACITY * sizeof(char *));
  size_t capacity = TOKENS_BASE_CAPACITY;
  size_t count = 0;
  char word_buf[MAX_WORD_LENGTH] = {0};
  size_t incr = 0;
  for (const char *p = buffer; *p; p++) {
    if (*p == '"') {
      do {
        word_buf[incr++] = *p;
        p++;
      } while (*p && *p != '"');
    }
    if (*p == '#') {
      while (*p && *p != '\n')
        p++;
    }
    if (*p == ' ' || *p == '\n' || *p == '\t') {
      if (incr > 0) {
        if (count == capacity) {
          capacity *= 2;
          tokens = realloc(tokens, capacity * sizeof(char *));
        }
        tokens[count++] = strdup(word_buf);
        incr = 0;
        memset(word_buf, 0, MAX_WORD_LENGTH);
      }
    } else {
      word_buf[incr++] = *p;
    }
  }
  if (incr > 0) {
    tokens[count++] = strdup(word_buf);
  }
  *token_count = count;
  return tokens;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Error: no input file\n");
    exit(1);
  }

  bool generate = true;
  bool assemble = true;
  bool link = true;
  bool run = false;

  FILE *in;
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-') {
      in = fopen(argv[i], "r");
      if (!in) {
        printf("Error: %s: ", argv[i]);
        perror("");
        return 1;
      }
    } else {
      if (argv[i][1] == 'S') {
        assemble = false;
        link = false;
      } else if (argv[i][1] == 'c') {
        link = false;
      } else if (argv[i][1] == 'r') {
        run = true;
      }
    }
  }

  if (!in) {
    printf("Error: no input file\n");
  }

  fseek(in, 0, SEEK_END);
  size_t file_size = ftell(in);
  fseek(in, 0, SEEK_SET);
  char *buffer = malloc(file_size + 1);
  size_t i = 0;
  char c;
  while ((c = fgetc(in)) != EOF) {
    buffer[i++] = c;
  }
  buffer[i] = '\0';
  fclose(in);
  size_t count;
  char **tokens = lex(buffer, &count);
  free(buffer);
  FILE *out = fopen("out.asm", "w");

  fprintf(out, "_start:\n");
  fprintf(out, " and rsp, -16\n call GetCommandLineW\n mov rcx, rax\n lea rdx, "
               "[rel argc]\n call "
               "CommandLineToArgvW\n mov [rel argv], rax\n");
  parse(tokens, count, out);
  if (err_flag) {
    fclose(out);

    for (size_t i = 0; i < string_table.count; i++) {
      free(string_table.strings[i]);
    }
    free(string_table.strings);
    free(tokens);

    remove("out.asm");
    exit(1);
  }
  if (print_int_called) {
    fprintf(out, "\nprint_integer:\n"
                 "    push rbp\n"
                 "    mov rbp, rsp\n"
                 "    sub rsp, 40\n"
                 "    lea rcx, [rel buffer]\n"
                 "    lea rdx, [rel format]\n"
                 "    mov r8, [rbp + 16]\n"
                 "    call wsprintfA\n"
                 "    mov rcx, 4294967285\n"
                 "    call GetStdHandle\n"
                 "    mov rcx, rax\n"
                 "    lea rdx, [rel buffer]\n"
                 "    mov r8, 1024\n"
                 "    call WriteConsoleA\n"
                 "    add rsp, 40\n"
                 "    pop rbp\n"
                 "    ret\n\n"
                 "section .data\n"
                 "    buffer db 1024 dup(0)\n"
                 "    format db \"%%d\", 10, 0\n");
  } else {
    fprintf(out, "section .data\n");
  }
  fprintf(out,
          "section .bss\n"
          "    argv resq 1\n"
          "    argc resd 1\n"
          "section .text\n"
          "    global _start\n"
          "    extern GetStdHandle, WriteConsoleA, WriteConsoleW, wsprintfA, "
          "ExitProcess, "
          "HeapAlloc, HeapReAlloc, HeapFree, GetProcessHeap, GetCommandLineW, "
          "CommandLineToArgvW, ");
  for (size_t i = 0; i < extern_count - 1; i++) {
    fprintf(out, "%s, ", externs[i]);
  }
  fprintf(out, "%s\n", externs[extern_count - 1]);
  output_string_table(out, &string_table);
  fclose(out);

  if (assemble) {
    system("nasm -fwin64 out.asm");
  }
  if (link) {
    // kernel32, user32, & shell32 linked for intrinsics
    system("ld out.obj -lkernel32 -luser32 -lshell32 -o result.exe");
    system("rm out.asm out.obj");
  }
  if (run) {
    system("result.exe");
  }

  for (size_t i = 0; i < string_table.count; i++) {
    free(string_table.strings[i]);
  }
  free(string_table.strings);
  free(tokens);
  return 0;
}
