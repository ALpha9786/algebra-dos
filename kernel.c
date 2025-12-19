#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define WHITE_ON_BLACK 0x0F
#define MAX_FILES 128
#define MAX_FILENAME 32
#define MAX_FILESIZE 4096
#define MAX_DIRS 64
#define MAX_PATH 256

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;

static uint16_t* vga = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0, cursor_y = 0;
static char input_buffer[256];
static int input_pos = 0;
static char current_dir[MAX_PATH] = "/";

typedef struct {
    char name[MAX_FILENAME];
    char path[MAX_PATH];
    char data[MAX_FILESIZE];
    uint32_t size;
    uint8_t is_dir;
    uint8_t used;
} File;

typedef struct {
    char name[MAX_FILENAME];
    char path[MAX_PATH];
    uint8_t used;
} Directory;

static File files[MAX_FILES];
static Directory dirs[MAX_DIRS];

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

int strncmp(const char* s1, const char* s2, int n) {
    while (n-- && *s1 && *s1 == *s2) { s1++; s2++; }
    return n < 0 ? 0 : *(unsigned char*)s1 - *(unsigned char*)s2;
}

void* memset(void* dest, int val, uint32_t count) {
    uint8_t* d = (uint8_t*)dest;
    while (count--) *d++ = (uint8_t)val;
    return dest;
}

void* memcpy(void* dest, const void* src, uint32_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (count--) *d++ = *s++;
    return dest;
}

void scroll_up() {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (WHITE_ON_BLACK << 8) | ' ';
    }
    cursor_y = VGA_HEIGHT - 1;
}

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (WHITE_ON_BLACK << 8) | ' ';
    cursor_x = cursor_y = 0;
}

void putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
    } else {
        vga[cursor_y * VGA_WIDTH + cursor_x] = (WHITE_ON_BLACK << 8) | c;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    if (cursor_y >= VGA_HEIGHT) scroll_up();
}

void print(const char* str) {
    while (*str) putchar(*str++);
}

void print_num(int32_t num) {
    if (num == 0) { putchar('0'); return; }
    if (num < 0) { putchar('-'); num = -num; }
    char buf[12];
    int i = 0;
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) putchar(buf[--i]);
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t shift_pressed = 0;

char scancode_to_char(uint8_t scancode) {
    static const char lower[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',
        0, ' '
    };
    
    static const char upper[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
        0, ' '
    };
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return 0;
    }
    
    if (scancode & 0x80) return 0;
    
    if (scancode < sizeof(lower)) {
        return shift_pressed ? upper[scancode] : lower[scancode];
    }
    return 0;
}

char get_key() {
    while (!(inb(0x64) & 0x01)) {
        asm volatile("pause");
    }
    
    uint8_t scancode = inb(0x60);
    return scancode_to_char(scancode);
}

void init_fs() {
    memset(files, 0, sizeof(files));
    memset(dirs, 0, sizeof(dirs));
    dirs[0].used = 1;
    strcpy(dirs[0].name, "/");
    strcpy(dirs[0].path, "/");
}

int find_file(const char* name, const char* path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0 && 
            strcmp(files[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int find_dir(const char* path) {
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dirs[i].used && strcmp(dirs[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

void cmd_ls() {
    print("Directory listing of ");
    print(current_dir);
    print(":\n");
    
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dirs[i].used) {
            int len = strlen(current_dir);
            if (strncmp(dirs[i].path, current_dir, len) == 0) {
                const char* subpath = dirs[i].path + len;
                if (strlen(subpath) > 0 && strcmp(dirs[i].path, current_dir) != 0) {
                    int has_slash = 0;
                    for (int j = (subpath[0] == '/' ? 1 : 0); subpath[j]; j++) {
                        if (subpath[j] == '/') { has_slash = 1; break; }
                    }
                    if (!has_slash) {
                        print("  [DIR]  ");
                        print(dirs[i].name);
                        print("\n");
                    }
                }
            }
        }
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].path, current_dir) == 0) {
            print("  [FILE] ");
            print(files[i].name);
            print(" (");
            print_num(files[i].size);
            print(" bytes)\n");
        }
    }
}

void cmd_mkdir(const char* name) {
    if (strlen(name) == 0) {
        print("Usage: mkdir <dirname>\n");
        return;
    }
    
    char fullpath[MAX_PATH];
    strcpy(fullpath, current_dir);
    if (fullpath[strlen(fullpath) - 1] != '/') strcat(fullpath, "/");
    strcat(fullpath, name);
    
    if (find_dir(fullpath) >= 0) {
        print("Error: Directory already exists\n");
        return;
    }
    
    for (int i = 0; i < MAX_DIRS; i++) {
        if (!dirs[i].used) {
            dirs[i].used = 1;
            strcpy(dirs[i].name, name);
            strcpy(dirs[i].path, fullpath);
            print("Directory created: ");
            print(fullpath);
            print("\n");
            return;
        }
    }
    print("Error: Maximum directories reached\n");
}

void cmd_cd(const char* path) {
    if (strlen(path) == 0 || strcmp(path, "/") == 0) {
        strcpy(current_dir, "/");
        return;
    }
    
    char newpath[MAX_PATH];
    if (path[0] == '/') {
        strcpy(newpath, path);
    } else {
        strcpy(newpath, current_dir);
        if (newpath[strlen(newpath) - 1] != '/') strcat(newpath, "/");
        strcat(newpath, path);
    }
    
    if (find_dir(newpath) >= 0) {
        strcpy(current_dir, newpath);
    } else {
        print("Error: Directory not found\n");
    }
}

void cmd_rm(const char* name) {
    if (strlen(name) == 0) {
        print("Usage: rm <filename>\n");
        return;
    }
    
    int idx = find_file(name, current_dir);
    if (idx >= 0) {
        files[idx].used = 0;
        print("File removed: ");
        print(name);
        print("\n");
    } else {
        print("Error: File not found\n");
    }
}

int is_digit(char c) { return c >= '0' && c <= '9'; }
int is_space(char c) { return c == ' ' || c == '\t'; }

int parse_number(const char** expr) {
    while (is_space(**expr)) (*expr)++;
    int num = 0, sign = 1;
    if (**expr == '-') { sign = -1; (*expr)++; }
    while (is_digit(**expr)) {
        num = num * 10 + (**expr - '0');
        (*expr)++;
    }
    return num * sign;
}

int eval_expr(const char* expr) {
    int result = parse_number(&expr);
    
    while (*expr) {
        while (is_space(*expr)) expr++;
        if (!*expr) break;
        
        char op = *expr++;
        int num = parse_number(&expr);
        
        switch (op) {
            case '+': result += num; break;
            case '-': result -= num; break;
            case '*': result *= num; break;
            case '/': if (num) result /= num; break;
            default: break;
        }
    }
    return result;
}

void solve_equation(const char* eq) {
    const char* p = eq;
    while (is_space(*p)) p++;
    
    if (*p != 'x') {
        print("Error: Equation must start with 'x'\n");
        return;
    }
    p++;
    while (is_space(*p)) p++;
    
    char op = *p++;
    int a = parse_number(&p);
    
    while (is_space(*p)) p++;
    if (*p != '=') {
        print("Error: Missing '=' sign\n");
        return;
    }
    p++;
    
    int b = parse_number(&p);
    int x = 0;
    
    switch (op) {
        case '+': x = b - a; break;
        case '-': x = b + a; break;
        case '*': x = a ? b / a : 0; break;
        case '/': x = b * a; break;
        default: print("Error: Invalid operator\n"); return;
    }
    
    print("x = ");
    print_num(x);
    print("\n");
}

void cmd_algebra(const char* expr) {
    if (strlen(expr) == 0) {
        print("Usage: algebra <expression> or algebra x + 6 = 3\n");
        return;
    }
    
    int has_x = 0, has_eq = 0;
    for (int i = 0; expr[i]; i++) {
        if (expr[i] == 'x') has_x = 1;
        if (expr[i] == '=') has_eq = 1;
    }
    
    if (has_x && has_eq) {
        solve_equation(expr);
    } else {
        int result = eval_expr(expr);
        print("Result: ");
        print_num(result);
        print("\n");
    }
}

void cmd_algebra_writeline(const char* args) {
    char filename[MAX_FILENAME];
    const char* expr = args;
    int i = 0;
    
    while (*expr && !is_space(*expr) && i < MAX_FILENAME - 1) {
        filename[i++] = *expr++;
    }
    filename[i] = '\0';
    
    while (is_space(*expr)) expr++;
    
    if (strlen(filename) == 0 || strlen(expr) == 0) {
        print("Usage: algebra-writeline <filename> <expression>\n");
        return;
    }
    
    char result_str[32];
    int result;
    int has_x = 0, has_eq = 0;
    for (int j = 0; expr[j]; j++) {
        if (expr[j] == 'x') has_x = 1;
        if (expr[j] == '=') has_eq = 1;
    }
    
    if (has_x && has_eq) {
        print("Note: Equation solving to file not fully implemented\n");
        return;
    } else {
        result = eval_expr(expr);
    }
    
    char* p = result_str;
    if (result < 0) { *p++ = '-'; result = -result; }
    if (result == 0) { *p++ = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (result > 0) {
            tmp[j++] = '0' + (result % 10);
            result /= 10;
        }
        while (j > 0) *p++ = tmp[--j];
    }
    *p++ = '\n';
    *p = '\0';
    
    int idx = find_file(filename, current_dir);
    if (idx < 0) {
        for (int j = 0; j < MAX_FILES; j++) {
            if (!files[j].used) {
                idx = j;
                files[j].used = 1;
                strcpy(files[j].name, filename);
                strcpy(files[j].path, current_dir);
                files[j].size = 0;
                break;
            }
        }
    }
    
    if (idx < 0) {
        print("Error: Cannot create file\n");
        return;
    }
    
    int len = strlen(result_str);
    if (files[idx].size + len < MAX_FILESIZE) {
        memcpy(files[idx].data + files[idx].size, result_str, len);
        files[idx].size += len;
        files[idx].data[files[idx].size] = '\0';
        print("Result written to ");
        print(filename);
        print("\n");
    } else {
        print("Error: File size limit exceeded\n");
    }
}

void cmd_cat(const char* filename) {
    int idx = find_file(filename, current_dir);
    if (idx >= 0) {
        for (uint32_t i = 0; i < files[idx].size; i++) {
            putchar(files[idx].data[i]);
        }
    } else {
        print("Error: File not found\n");
    }
}

void cmd_ping(const char* host) {
    print("PING ");
    print(host);
    print(" - Simulated network test\n");
    print("Reply from ");
    print(host);
    print(": bytes=32 time<1ms TTL=64\n");
    print("Reply from ");
    print(host);
    print(": bytes=32 time<1ms TTL=64\n");
    print("Reply from ");
    print(host);
    print(": bytes=32 time<1ms TTL=64\n");
}

void process_command(char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;
    
    char* args = cmd;
    while (*args && *args != ' ') args++;
    if (*args) {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
    }
    
    if (strcmp(cmd, "help") == 0) {
        print("Available commands:\n");
        print("  ls - List directory\n");
        print("  cd <dir> - Change directory\n");
        print("  mkdir <n> - Create directory\n");
        print("  rm <file> - Remove file\n");
        print("  cat <file> - Display file contents\n");
        print("  ping <host> - Ping simulation\n");
        print("  algebra <expr> - Calculate expression\n");
        print("  algebra-writeline <file> <expr> - Write result to file\n");
        print("  clear - Clear screen\n");
        print("  help - Show this help\n");
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        cmd_ls();
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd(args);
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(args);
    } else if (strcmp(cmd, "rm") == 0) {
        cmd_rm(args);
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(cmd, "ping") == 0) {
        cmd_ping(args);
    } else if (strcmp(cmd, "algebra") == 0) {
        cmd_algebra(args);
    } else if (strcmp(cmd, "algebra-writeline") == 0) {
        cmd_algebra_writeline(args);
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else {
        print("Unknown command: ");
        print(cmd);
        print("\n");
    }
}

void shell() {
    print("Algebra OS v2.0 - Type 'help' for commands\n\n");
    
    while (1) {
        print(current_dir);
        print(" $ ");
        
        input_pos = 0;
        memset(input_buffer, 0, sizeof(input_buffer));
        
        while (1) {
            char c = get_key();
            if (c) {
                if (c == '\n') {
                    putchar('\n');
                    input_buffer[input_pos] = '\0';
                    if (input_pos > 0) {
                        process_command(input_buffer);
                    }
                    break;
                } else if (c == '\b') {
                    if (input_pos > 0) {
                        input_pos--;
                        putchar('\b');
                        putchar(' ');
                        putchar('\b');
                    }
                } else if ((uint32_t)input_pos < sizeof(input_buffer) - 1 && c >= 32 && c <= 126) {
                    input_buffer[input_pos++] = c;
                    putchar(c);
                }
            }
        }
    }
}

void kernel_main() {
    clear_screen();
    init_fs();
    shell();
}

struct multiboot_header {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
} __attribute__((packed));

__attribute__((section(".multiboot")))
struct multiboot_header mb_header = {
    .magic = 0x1BADB002,
    .flags = 0x00000000,
    .checksum = -(0x1BADB002 + 0x00000000)
};

__attribute__((section(".text.boot")))
void _start() {
    kernel_main();
}
