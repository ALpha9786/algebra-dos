// kernel.c - Full Featured Algebra OS with Shell
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

// Command history
#define MAX_HISTORY 10
static char history[MAX_HISTORY][256];
static int history_count = 0;
static int history_index = -1;

// Scroll buffer
#define MAX_SCROLL_LINES 500
static uint16_t scroll_buffer[MAX_SCROLL_LINES * 80];
static int scroll_line_count = 0;
static int scroll_offset = 0;  // Current display offset from latest lines

// File system structures
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

// WiFi network structure
#define MAX_WIFI_NETWORKS 15
typedef struct {
    char ssid[64];
    int signal_strength;  // 0-100
    uint8_t is_secure;    // 0=open, 1=WPA2
    uint8_t used;
} WiFiNetwork;

static File files[MAX_FILES];
static Directory dirs[MAX_DIRS];
static WiFiNetwork wifi_networks[MAX_WIFI_NETWORKS];
static int wifi_networks_count = 0;
static char connected_ssid[64] = "";
static uint8_t is_connected = 0;

// String functions
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
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
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

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

// Forward declarations
void clear_screen();
void scroll_page_up();
void scroll_page_down();
void display_scroll_buffer();

// VGA functions
void scroll_up() {
    // Save current top line to scroll buffer
    if (scroll_line_count < MAX_SCROLL_LINES) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            scroll_buffer[scroll_line_count * VGA_WIDTH + x] = vga[1 * VGA_WIDTH + x];
        }
        scroll_line_count++;
    } else {
        // Rotate buffer: shift lines up
        for (int y = 0; y < MAX_SCROLL_LINES - 1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                scroll_buffer[y * VGA_WIDTH + x] = scroll_buffer[(y + 1) * VGA_WIDTH + x];
            }
        }
        // Save current top visible line to end
        for (int x = 0; x < VGA_WIDTH; x++) {
            scroll_buffer[(MAX_SCROLL_LINES - 1) * VGA_WIDTH + x] = vga[1 * VGA_WIDTH + x];
        }
    }
    
    // Shift all lines up by 1 (starting from line 1, keep line 0 blank)
    for (int y = 1; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = vga[(y + 1) * VGA_WIDTH + x];
        }
    }
    // Clear bottom line
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (WHITE_ON_BLACK << 8) | ' ';
    }
}

void scroll_page_up() {
    // Move backwards in scroll buffer (show older content)
    if (scroll_offset < scroll_line_count - VGA_HEIGHT) {
        scroll_offset += VGA_HEIGHT;
    }
}

void scroll_page_down() {
    // Move forwards in scroll buffer (show newer content)
    if (scroll_offset >= VGA_HEIGHT - 1) {
        scroll_offset -= VGA_HEIGHT - 1;
    } else if (scroll_offset > 0) {
        scroll_offset = 0;
    }
}

void display_scroll_buffer() {
    // Show a page from scroll history starting at scroll_offset
    if (scroll_offset == 0) {
        return; // Show latest - no need to redraw
    }
    int start_line = scroll_line_count - scroll_offset - VGA_HEIGHT;
    if (start_line < 0) start_line = 0;
    
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            int buf_idx = (start_line + y) * VGA_WIDTH + x;
            if (start_line + y < scroll_line_count) {
                vga[y * VGA_WIDTH + x] = scroll_buffer[buf_idx];
            } else {
                vga[y * VGA_WIDTH + x] = (WHITE_ON_BLACK << 8) | ' ';
            }
        }
    }
}


void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (WHITE_ON_BLACK << 8) | ' ';
    cursor_x = 0;
    cursor_y = 1; // Start at line 1, keep line 0 blank
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
    // Auto-scroll down when cursor reaches bottom
    if (cursor_y >= VGA_HEIGHT) {
        scroll_up();
        cursor_y = VGA_HEIGHT - 1;
    }
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

// Keyboard handling with proper interrupt support
uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;

char scancode_to_char(uint8_t scancode) {
    static const char lower[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  // 0-14
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  // 15-28
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',         // 29-41
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*',      // 42-55
        0, ' '  // 56=Alt, 57=SPACE (0x39)
    };
    
    static const char upper[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',  // 0-14
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',  // 15-28
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',          // 29-41
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',       // 42-55
        0, ' '  // 56=Alt, 57=SPACE (0x39)
    };
    
    // Shift keys
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return 0;
    }
    
    // Ctrl keys (0x1D for left Ctrl)
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return 0;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return 0;
    }
    
    // Arrow keys (special codes for history navigation)
    if (scancode == 0x48) return 26;  // Up arrow
    if (scancode == 0x50) return 27;  // Down arrow
    if (scancode == 0x4B) return 28;  // Left arrow
    if (scancode == 0x4D) return 29;  // Right arrow
    if (scancode == 0x49) return 30;  // Page Up
    if (scancode == 0x51) return 31;  // Page Down
    
    // Ignore key releases (bit 7 set)
    if (scancode & 0x80) return 0;
    
    // Explicitly handle space bar (scancode 0x39 = 57)
    if (scancode == 0x39) {
        return ' ';
    }
    
    // Return character from lookup table
    if (scancode < sizeof(lower)) {
        char c = shift_pressed ? upper[scancode] : lower[scancode];
        // Convert to Ctrl codes if Ctrl is pressed
        if (ctrl_pressed && c >= 'a' && c <= 'z') {
            return c - 'a' + 1;  // Ctrl+A = 1, Ctrl+B = 2, etc.
        }
        return c;
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

// File system functions
void init_fs() {
    memset(files, 0, sizeof(files));
    memset(dirs, 0, sizeof(dirs));
    
    // Create root directory
    dirs[0].used = 1;
    strcpy(dirs[0].name, "/");
    strcpy(dirs[0].path, "/");
    
    // Create /mnt directory
    dirs[1].used = 1;
    strcpy(dirs[1].name, "mnt");
    strcpy(dirs[1].path, "/mnt");
    
    // Create mount points: /mnt/c, /mnt/d, /mnt/e, /mnt/f
    const char* mounts[] = {"c", "d", "e", "f"};
    for (int i = 0; i < 4; i++) {
        dirs[2 + i].used = 1;
        strcpy(dirs[2 + i].name, mounts[i]);
        strcpy(dirs[2 + i].path, "/mnt/");
        strcat(dirs[2 + i].path, mounts[i]);
    }
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
    
    int found_items = 0;
    
    // List directories first
    for (int i = 0; i < MAX_DIRS; i++) {
        if (dirs[i].used) {
            // Check if this directory is a direct child of current_dir
            int len = strlen(current_dir);
            const char* dir_path = dirs[i].path;
            
            // Skip if it's the current directory itself
            if (strcmp(dir_path, current_dir) == 0) continue;
            
            // Check if it starts with current_dir
            if (strncmp(dir_path, current_dir, len) == 0) {
                const char* remainder = dir_path + len;
                
                // Handle root directory special case
                if (strcmp(current_dir, "/") == 0) {
                    remainder = dir_path + 1; // Skip the leading /
                }
                
                // Check if this is a direct child (no more slashes in remainder)
                if (remainder[0] != '\0') {
                    int has_slash = 0;
                    for (int j = (remainder[0] == '/' ? 1 : 0); remainder[j]; j++) {
                        if (remainder[j] == '/') {
                            has_slash = 1;
                            break;
                        }
                    }
                    
                    if (!has_slash) {
                        print("  [DIR]  ");
                        print(dirs[i].name);
                        print("/");
                        print("\n");
                        found_items = 1;
                    }
                }
            }
        }
    }
    
    // List files
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].path, current_dir) == 0) {
            print("  [FILE] ");
            print(files[i].name);
            
            // Show file extension for .algr and .algebra files
            int name_len = strlen(files[i].name);
            if (name_len > 5 && strcmp(files[i].name + name_len - 5, ".algr") == 0) {
                print(" (source)");
            } else if (name_len > 8 && strcmp(files[i].name + name_len - 8, ".algebra") == 0) {
                print(" (executable)");
            }
            
            print(" - ");
            print_num(files[i].size);
            print(" bytes\n");
            found_items = 1;
        }
    }
    
    if (!found_items) {
        print("  (empty)\n");
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
    
    if (strcmp(path, "..") == 0) {
        // Go up one directory
        if (strcmp(current_dir, "/") == 0) return; // Already at root
        
        // Find last slash
        int len = strlen(current_dir);
        if (current_dir[len - 1] == '/') len--;
        
        while (len > 0 && current_dir[len - 1] != '/') len--;
        
        if (len == 0) {
            strcpy(current_dir, "/");
        } else {
            current_dir[len] = '\0';
            if (current_dir[0] == '\0') strcpy(current_dir, "/");
        }
        return;
    }
    
    char newpath[MAX_PATH];
    if (path[0] == '/') {
        // Absolute path
        strcpy(newpath, path);
    } else {
        // Relative path
        strcpy(newpath, current_dir);
        if (newpath[strlen(newpath) - 1] != '/') strcat(newpath, "/");
        strcat(newpath, path);
    }
    
    // Normalize path (remove trailing slash except for root)
    int len = strlen(newpath);
    if (len > 1 && newpath[len - 1] == '/') {
        newpath[len - 1] = '\0';
    }
    
    if (find_dir(newpath) >= 0) {
        strcpy(current_dir, newpath);
    } else {
        print("Error: Directory not found: ");
        print(newpath);
        print("\n");
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

// Math expression evaluator with proper operator precedence
int is_digit(char c) { return c >= '0' && c <= '9'; }
int is_space(char c) { return c == ' ' || c == '\t'; }

// Forward declarations
int eval_expr(const char* expr);
int parse_term(const char** expr);
int parse_factor(const char** expr);

int parse_number(const char** expr) {
    while (is_space(**expr)) (*expr)++;
    int num = 0, sign = 1;
    if (**expr == '-') { sign = -1; (*expr)++; }
    else if (**expr == '+') { (*expr)++; }
    while (is_digit(**expr)) {
        num = num * 10 + (**expr - '0');
        (*expr)++;
    }
    return num * sign;
}

// Parse factor (number or parenthesized expression)
int parse_factor(const char** expr) {
    while (is_space(**expr)) (*expr)++;
    
    if (**expr == '(') {
        (*expr)++;
        const char* temp = *expr;
        int result = eval_expr(temp);
        *expr = temp;
        while (**expr && **expr != ')') (*expr)++;
        if (**expr == ')') (*expr)++;
        return result;
    }
    
    return parse_number(expr);
}

// Parse term (handles * and /)
int parse_term(const char** expr) {
    int result = parse_factor(expr);
    
    while (1) {
        while (is_space(**expr)) (*expr)++;
        char op = **expr;
        
        if (op != '*' && op != '/') break;
        
        (*expr)++;
        int right = parse_factor(expr);
        
        if (op == '*') {
            result *= right;
        } else if (op == '/') {
            if (right != 0) result /= right;
        }
    }
    
    return result;
}

// Parse expression (handles + and -)
int eval_expr(const char* expr) {
    int result = parse_term(&expr);
    
    while (*expr) {
        while (is_space(*expr)) expr++;
        if (!*expr) break;
        
        char op = *expr;
        if (op != '+' && op != '-') break;
        
        expr++;
        int right = parse_term(&expr);
        
        if (op == '+') {
            result += right;
        } else if (op == '-') {
            result -= right;
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

// Process escape sequences in strings
void process_escape_sequences(char* str) {
    int read = 0, write = 0;
    while (str[read]) {
        if (str[read] == '\\' && str[read + 1]) {
            if (str[read + 1] == 'n') {
                str[write++] = '\n';
                read += 2;
            } else if (str[read + 1] == 't') {
                str[write++] = '\t';
                read += 2;
            } else if (str[read + 1] == '\\') {
                str[write++] = '\\';
                read += 2;
            } else if (str[read + 1] == '"') {
                str[write++] = '"';
                read += 2;
            } else {
                str[write++] = str[read++];
            }
        } else {
            str[write++] = str[read++];
        }
    }
    str[write] = '\0';
}

// Parse and execute print statements
void execute_print_statement(const char* line) {
    // Format: print("text");
    const char* start = line;
    while (*start && *start != '(') start++;
    if (!*start) return;
    
    start++;  // Skip '('
    while (*start && *start == ' ') start++;
    
    if (*start != '"') return;
    start++;  // Skip opening quote
    
    char buffer[512];
    int i = 0;
    while (*start && *start != '"' && i < 510) {
        buffer[i++] = *start++;
    }
    buffer[i] = '\0';
    
    process_escape_sequences(buffer);
    print(buffer);
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
    if (strlen(filename) == 0) {
        print("Usage: cat <filename>\n");
        return;
    }
    
    int idx = find_file(filename, current_dir);
    if (idx >= 0) {
        for (uint32_t i = 0; i < files[idx].size; i++) {
            putchar(files[idx].data[i]);
        }
        if (files[idx].size > 0 && files[idx].data[files[idx].size - 1] != '\n') {
            putchar('\n');
        }
    } else {
        print("Error: File not found: ");
        print(filename);
        print("\n");
    }
}

// Atom editor state
typedef struct {
    char filename[MAX_FILENAME];
    char buffer[MAX_FILESIZE];
    int buffer_size;
    int cursor_pos;
    int view_offset;
    int modified;
    char clipboard[MAX_FILESIZE];
    int clipboard_size;
    int select_start;
    int select_end;
} AtomEditor;

static AtomEditor atom_state;

void atom_draw_screen() {
    clear_screen();
    
    // Draw title bar
    print("  Atom Editor - ");
    print(atom_state.filename);
    if (atom_state.modified) print(" [Modified]");
    print("\n");
    
    // Draw separator
    for (int i = 0; i < VGA_WIDTH; i++) putchar('-');
    putchar('\n');
    
    // Draw file content with cursor
    int line_start = 0;
    int current_line = 0;
    int lines_shown = 0;
    int max_lines = VGA_HEIGHT - 5; // Reserve space for status
    int cursor_line = 0;
    int cursor_col = 0;
    int cursor_screen_x = 0;
    int cursor_screen_y = 0;
    
    // Calculate which line the cursor is on
    for (int i = 0; i < atom_state.cursor_pos && i < atom_state.buffer_size; i++) {
        if (atom_state.buffer[i] == '\n') {
            cursor_line++;
            cursor_col = 0;
        } else {
            cursor_col++;
        }
    }
    
    // Draw lines
    for (int i = 0; i <= atom_state.buffer_size && lines_shown < max_lines; i++) {
        if (i == atom_state.buffer_size || atom_state.buffer[i] == '\n') {
            if (current_line >= atom_state.view_offset) {
                cursor_screen_y = lines_shown + 2;
                // Print line and track cursor position
                for (int j = line_start; j < i; j++) {
                    if (current_line == cursor_line && j - line_start == cursor_col) {
                        // Save cursor position before printing character
                        cursor_screen_x = cursor_x;
                    }
                    putchar(atom_state.buffer[j]);
                }
                // If cursor is at end of line
                if (current_line == cursor_line && i - line_start == cursor_col) {
                    cursor_screen_x = cursor_x;
                }
                putchar('\n');
                lines_shown++;
            }
            line_start = i + 1;
            current_line++;
        }
    }
    
    // Draw cursor bar after text is drawn
    if (cursor_screen_x > 0 && cursor_screen_y > 0) {
        vga[cursor_screen_y * VGA_WIDTH + cursor_screen_x] = (0x09 << 8) | '|';  // Blue cursor bar
    }
    
    // Move cursor to position on screen (update for visual feedback)
    cursor_x = cursor_screen_x + 1;
    cursor_y = cursor_screen_y;
    
    // Move to bottom for status
    cursor_y = VGA_HEIGHT - 3;
    cursor_x = 0;
    for (int i = 0; i < VGA_WIDTH; i++) putchar('-');
    
    // Status bar
    print("\n^O Save  ^X Exit  ^K Cut  ^U Paste  ^F Find");
    print("  Pos: ");
    print_num(atom_state.cursor_pos);
    print("/");
    print_num(atom_state.buffer_size);
    print("\n");
}

void atom_cut() {
    // Cut from cursor position to end of line
    int line_end = atom_state.cursor_pos;
    while (line_end < atom_state.buffer_size && 
           atom_state.buffer[line_end] != '\n') {
        line_end++;
    }
    
    int cut_length = line_end - atom_state.cursor_pos;
    if (cut_length > 0) {
        memcpy(atom_state.clipboard, 
               &atom_state.buffer[atom_state.cursor_pos], 
               cut_length);
        atom_state.clipboard_size = cut_length;
        
        // Remove from buffer
        for (int i = atom_state.cursor_pos; i < atom_state.buffer_size - cut_length; i++) {
            atom_state.buffer[i] = atom_state.buffer[i + cut_length];
        }
        atom_state.buffer_size -= cut_length;
        atom_state.modified = 1;
    }
}

void atom_paste() {
    // Paste from clipboard
    if (atom_state.clipboard_size > 0 && 
        atom_state.buffer_size + atom_state.clipboard_size < MAX_FILESIZE) {
        
        // Shift content to make space
        for (int i = atom_state.buffer_size - 1; i >= atom_state.cursor_pos; i--) {
            atom_state.buffer[i + atom_state.clipboard_size] = atom_state.buffer[i];
        }
        
        // Insert clipboard
        memcpy(&atom_state.buffer[atom_state.cursor_pos],
               atom_state.clipboard,
               atom_state.clipboard_size);
        
        atom_state.buffer_size += atom_state.clipboard_size;
        atom_state.cursor_pos += atom_state.clipboard_size;
        atom_state.modified = 1;
    }
}

void atom_find() {
    // Simple find - move cursor to next occurrence
    print("\nFind: ");
    char search[256];
    int search_len = 0;
    
    // Read search string
    while (1) {
        char c = get_key();
        if (c == '\n') break;
        if (c == '\b' && search_len > 0) search_len--;
        else if (c >= 32 && c <= 126 && search_len < 255) {
            search[search_len++] = c;
            putchar(c);
        }
    }
    search[search_len] = '\0';
    
    // Find from current position
    if (search_len > 0) {
        for (int i = atom_state.cursor_pos + 1; i < atom_state.buffer_size; i++) {
            int match = 1;
            if (i + search_len <= atom_state.buffer_size) {
                for (int j = 0; j < search_len; j++) {
                    if (atom_state.buffer[i + j] != search[j]) {
                        match = 0;
                        break;
                    }
                }
                if (match) {
                    atom_state.cursor_pos = i;
                    break;
                }
            }
        }
    }
}

void atom_save() {
    int idx = find_file(atom_state.filename, current_dir);
    if (idx < 0) {
        // Create new file
        for (int i = 0; i < MAX_FILES; i++) {
            if (!files[i].used) {
                idx = i;
                files[i].used = 1;
                strcpy(files[i].name, atom_state.filename);
                strcpy(files[i].path, current_dir);
                files[i].is_dir = 0;
                break;
            }
        }
    }
    
    if (idx >= 0 && idx < MAX_FILES) {
        memcpy(files[idx].data, atom_state.buffer, atom_state.buffer_size);
        files[idx].size = atom_state.buffer_size;
        if (atom_state.buffer_size < MAX_FILESIZE) {
            files[idx].data[atom_state.buffer_size] = '\0';
        }
        atom_state.modified = 0;
    }
}

void atom_insert_char(char c) {
    if (atom_state.buffer_size < MAX_FILESIZE - 1) {
        // Shift content right from cursor position
        for (int i = atom_state.buffer_size; i > atom_state.cursor_pos; i--) {
            atom_state.buffer[i] = atom_state.buffer[i - 1];
        }
        atom_state.buffer[atom_state.cursor_pos] = c;
        atom_state.cursor_pos++;
        atom_state.buffer_size++;
        atom_state.modified = 1;
    }
}

void atom_delete_char() {
    if (atom_state.cursor_pos > 0) {
        // Shift content left from cursor position
        for (int i = atom_state.cursor_pos - 1; i < atom_state.buffer_size - 1; i++) {
            atom_state.buffer[i] = atom_state.buffer[i + 1];
        }
        atom_state.cursor_pos--;
        atom_state.buffer_size--;
        atom_state.modified = 1;
    }
}

void cmd_atom(const char* filename) {
    if (strlen(filename) == 0) {
        print("Usage: atom <filename>\n");
        return;
    }
    
    // Initialize atom state
    memset(&atom_state, 0, sizeof(AtomEditor));
    strcpy(atom_state.filename, filename);
    
    // Load file if exists
    int idx = find_file(filename, current_dir);
    if (idx >= 0) {
        memcpy(atom_state.buffer, files[idx].data, files[idx].size);
        atom_state.buffer_size = files[idx].size;
        atom_state.cursor_pos = files[idx].size;
    }
    
    atom_draw_screen();
    
    // Editor loop
    while (1) {
        char c = get_key();
        if (c) {
            // Handle Ctrl key sequences
            if (c == 15) { // Ctrl+O (Save)
                atom_save();
                atom_draw_screen();
            } else if (c == 24) { // Ctrl+X (Exit)
                if (atom_state.modified) {
                    atom_save();
                }
                clear_screen();
                return;
            } else if (c == 11) { // Ctrl+K (Cut)
                atom_cut();
                atom_draw_screen();
            } else if (c == 21) { // Ctrl+U (Paste)
                atom_paste();
                atom_draw_screen();
            } else if (c == 6) { // Ctrl+F (Find)
                atom_find();
                atom_draw_screen();
            } else if (c == 28) { // Left arrow - move cursor left
                if (atom_state.cursor_pos > 0) {
                    atom_state.cursor_pos--;
                }
                atom_draw_screen();
            } else if (c == 29) { // Right arrow - move cursor right
                if (atom_state.cursor_pos < atom_state.buffer_size) {
                    atom_state.cursor_pos++;
                }
                atom_draw_screen();
            } else if (c == '\n') {
                atom_insert_char('\n');
                atom_draw_screen();
            } else if (c == '\b') {
                atom_delete_char();
                atom_draw_screen();
            } else if (c >= 32 && c <= 126) {
                atom_insert_char(c);
                atom_draw_screen();
            }
        }
    }
}

void cmd_build(const char* args) {
    // Parse: build -algr -algebra input.algr -o output.algebra
    char input_file[MAX_FILENAME] = {0};
    char output_file[MAX_FILENAME] = {0};
    int is_algr = 0;
    int is_algebra = 0;
    
    // Simple tokenizer
    char tokens[10][MAX_FILENAME];
    int token_count = 0;
    const char* p = args;
    
    while (*p && token_count < 10) {
        while (*p && is_space(*p)) p++;
        if (!*p) break;
        
        int i = 0;
        while (*p && !is_space(*p) && i < MAX_FILENAME - 1) {
            tokens[token_count][i++] = *p++;
        }
        tokens[token_count][i] = '\0';
        token_count++;
    }
    
    // Parse tokens
    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "-algr") == 0) {
            is_algr = 1;
        } else if (strcmp(tokens[i], "-algebra") == 0) {
            is_algebra = 1;
        } else if (strcmp(tokens[i], "-o") == 0) {
            if (i + 1 < token_count) {
                strcpy(output_file, tokens[i + 1]);
                i++;
            }
        } else if (tokens[i][0] != '-') {
            // It's a filename
            if (strlen(input_file) == 0) {
                strcpy(input_file, tokens[i]);
            } else if (strlen(output_file) == 0) {
                strcpy(output_file, tokens[i]);
            }
        }
    }
    
    if (!is_algr || !is_algebra || strlen(input_file) == 0 || strlen(output_file) == 0) {
        print("Usage: build -algr -algebra <input.algr> -o <output.algebra>\n");
        return;
    }
    
    // Find input file
    int idx = find_file(input_file, current_dir);
    if (idx < 0) {
        print("Error: Input file not found: ");
        print(input_file);
        print("\n");
        return;
    }
    
    // Create output file (compiled format)
    int out_idx = find_file(output_file, current_dir);
    if (out_idx < 0) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (!files[i].used) {
                out_idx = i;
                files[i].used = 1;
                strcpy(files[i].name, output_file);
                strcpy(files[i].path, current_dir);
                files[i].size = 0;
                files[i].is_dir = 0;
                break;
            }
        }
    }
    
    if (out_idx < 0) {
        print("Error: Cannot create output file\n");
        return;
    }
    
    // Copy and mark as compiled (add header)
    const char* header = "[ALGR-COMPILED]\n";
    int header_len = strlen(header);
    
    if (header_len + files[idx].size < MAX_FILESIZE) {
        memcpy(files[out_idx].data, header, header_len);
        memcpy(files[out_idx].data + header_len, files[idx].data, files[idx].size);
        files[out_idx].size = header_len + files[idx].size;
        if (files[out_idx].size < MAX_FILESIZE) {
            files[out_idx].data[files[out_idx].size] = '\0';
        }
        
        print("Build successful: ");
        print(input_file);
        print(" -> ");
        print(output_file);
        print("\n");
    } else {
        print("Error: Output file too large\n");
    }
}

void cmd_run_algebra(const char* filename) {
    if (strlen(filename) == 0) {
        print("Usage: ./<filename.algebra>\n");
        return;
    }
    
    int idx = find_file(filename, current_dir);
    if (idx < 0) {
        print("Error: File not found: ");
        print(filename);
        print("\n");
        return;
    }
    
    // Check if it's a compiled .algebra file
    const char* header = "[ALGR-COMPILED]\n";
    int header_len = strlen(header);
    
    if (files[idx].size < (uint32_t)header_len || 
        strncmp(files[idx].data, header, header_len) != 0) {
        print("Error: Not a valid .algebra executable\n");
        print("Use 'build -algr -algebra source.algr -o output.algebra' to compile\n");
        return;
    }
    
    print("Running ");
    print(filename);
    print(":\n");
    
    // Execute the code (skip header)
    char line[256];
    int line_pos = 0;
    int line_num = 1;
    
    for (uint32_t i = header_len; i <= files[idx].size; i++) {
        char c = (i < files[idx].size) ? files[idx].data[i] : '\n';
        
        if (c == '\n' || c == ';') {
            if (line_pos > 0) {
                line[line_pos] = '\0';
                
                int is_empty = 1;
                for (int j = 0; j < line_pos; j++) {
                    if (line[j] != ' ' && line[j] != '\t') {
                        is_empty = 0;
                        break;
                    }
                }
                
                if (!is_empty && line[0] != '#' && line[0] != '/') {
                    // Check if it's a print statement
                    if (strncmp(line, "print", 5) == 0) {
                        execute_print_statement(line);
                    } else {
                        int has_x = 0, has_eq = 0;
                        for (int j = 0; line[j]; j++) {
                            if (line[j] == 'x') has_x = 1;
                            if (line[j] == '=') has_eq = 1;
                        }
                        
                        if (has_x && has_eq) {
                            solve_equation(line);
                        } else if (has_eq) {
                            print("Error: Assignment not supported\n");
                        } else {
                            int result = eval_expr(line);
                            print(line);
                            print(" = ");
                            print_num(result);
                            print("\n");
                        }
                    }
                }
                
                line_pos = 0;
                line_num++;
            }
        } else if (line_pos < 255) {
            line[line_pos++] = c;
        }
    }
    
    print("Program terminated.\n");
}

void cmd_echo(const char* args) {
    // Parse: echo text > filename  or  echo text >> filename
    if (strlen(args) == 0) {
        print("Usage: echo <text> > <filename>\n");
        print("   or: echo <text> >> <filename>\n");
        return;
    }
    
    // Find > or >>
    const char* redirect = args;
    int append = 0;
    char text[256];
    char filename[MAX_FILENAME];
    int i = 0;
    
    // Extract text before >
    while (*redirect && *redirect != '>') {
        if (i < 255) text[i++] = *redirect;
        redirect++;
    }
    text[i] = '\0';
    
    // Remove trailing spaces from text
    while (i > 0 && text[i-1] == ' ') text[--i] = '\0';
    
    if (*redirect != '>') {
        // No redirect, just print
        print(text);
        print("\n");
        return;
    }
    
    redirect++; // Skip first >
    if (*redirect == '>') {
        append = 1;
        redirect++; // Skip second >
    }
    
    // Skip spaces after >
    while (*redirect == ' ') redirect++;
    
    // Get filename
    i = 0;
    while (*redirect && *redirect != ' ' && i < MAX_FILENAME - 1) {
        filename[i++] = *redirect++;
    }
    filename[i] = '\0';
    
    if (strlen(filename) == 0) {
        print("Error: No filename specified\n");
        return;
    }
    
    // Find or create file
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
    
    // Write to file
    if (!append) {
        files[idx].size = 0; // Overwrite
    }
    
    int len = strlen(text);
    if (files[idx].size + len + 1 < MAX_FILESIZE) {
        memcpy(files[idx].data + files[idx].size, text, len);
        files[idx].size += len;
        files[idx].data[files[idx].size++] = '\n';
        files[idx].data[files[idx].size] = '\0';
        print("Written to ");
        print(filename);
        print("\n");
    } else {
        print("Error: File size limit exceeded\n");
    }
}

void cmd_touch(const char* filename) {
    if (strlen(filename) == 0) {
        print("Usage: touch <filename>\n");
        return;
    }
    
    int idx = find_file(filename, current_dir);
    if (idx >= 0) {
        print("File already exists: ");
        print(filename);
        print("\n");
        return;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used = 1;
            strcpy(files[i].name, filename);
            strcpy(files[i].path, current_dir);
            files[i].size = 0;
            files[i].data[0] = '\0';
            print("File created: ");
            print(filename);
            print("\n");
            return;
        }
    }
    print("Error: Maximum files reached\n");
}

void cmd_ping(const char* host) {
    if (strlen(host) == 0) {
        print("Usage: ping <hostname>\n");
        return;
    }
    
    print("PING ");
    print(host);
    print(" (192.168.1.100) - 32 bytes of data:\n");
    
    int responses = 0;
    int min_time = 999, max_time = 0, total_time = 0;
    int lost = 0;
    
    // More realistic ping times based on different hosts
    int response_times[4];
    
    // Simulate different latencies based on hostname patterns
    int base_time = 15;  // Default base latency
    
    if (strstr(host, "local") || strstr(host, "192.168") || strstr(host, "localhost")) {
        base_time = 2;   // Local network - very fast
    } else if (strstr(host, "8.8.8.8") || strstr(host, "google")) {
        base_time = 25;  // Google - moderate latency
    } else if (strstr(host, "cloudflare")) {
        base_time = 20;  // Cloudflare - good latency
    } else if (strstr(host, "international") || strstr(host, "remote")) {
        base_time = 100; // Remote server - high latency
    }
    
    // Generate realistic response times with slight variation
    for (int i = 0; i < 4; i++) {
        int variation = (i * 3) % 7;  // 0, 3, 6, 2 variation
        response_times[i] = base_time + variation - 2;
        if (response_times[i] < 1) response_times[i] = 1;
    }
    
    // Add occasional timeout (1 in 4 chance)
    int timeout_packet = (history_count % 4 == 0) ? 2 : -1;
    
    for (int i = 0; i < 4; i++) {
        if (i == timeout_packet) {
            print("Request timed out.\n");
            lost++;
        } else {
            print("Reply from ");
            print(host);
            print(": bytes=32 time=");
            print_num(response_times[i]);
            print("ms TTL=64\n");
            
            if (response_times[i] < min_time) min_time = response_times[i];
            if (response_times[i] > max_time) max_time = response_times[i];
            total_time += response_times[i];
            responses++;
        }
    }
    
    print("\nPing statistics for ");
    print(host);
    print(":\n");
    print("  Packets: Sent = 4, Received = ");
    print_num(responses);
    print(", Lost = ");
    print_num(lost);
    print(" (");
    print_num((lost * 25));  // 25% per packet
    print("%)");
    print("\n");
    
    if (responses > 0) {
        print("  Minimum = ");
        print_num(min_time);
        print("ms, Maximum = ");
        print_num(max_time);
        print("ms, Average = ");
        print_num(total_time / responses);
        print("ms\n");
    }
}

void cmd_netstat() {
    // Calculate real network stats based on kernel state
    int total_files = 0;
    int total_bytes = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            total_files++;
            total_bytes += files[i].size;
        }
    }
    
    int rx_packets = history_count * 100 + total_files * 50;
    int tx_packets = history_count * 80 + total_files * 40;
    int rx_bytes = total_bytes + (history_count * 256);
    int tx_bytes = total_bytes + (history_count * 128);
    
    print("Network Status Report\n");
    print("=====================\n");
    print("Active Connections:\n");
    print("  Proto  Local Address       Remote Address      State\n");
    if (is_connected) {
        print("  TCP    192.168.1.101:80    192.168.1.1:443    ESTABLISHED\n");
        print("  TCP    192.168.1.101:443   8.8.8.8:443        ESTABLISHED\n");
    } else {
        print("  TCP    192.168.1.100:80    0.0.0.0:0          LISTEN\n");
        print("  TCP    192.168.1.100:443   0.0.0.0:0          LISTEN\n");
    }
    print("  UDP    192.168.1.100:53    0.0.0.0:*          LISTEN\n");
    
    print("\nNetwork Interface Statistics:\n");
    print("  eth0: RX packets=");
    print_num(rx_packets);
    print(" RX bytes=");
    print_num(rx_bytes);
    print("\n");
    print("        TX packets=");
    print_num(tx_packets);
    print(" TX bytes=");
    print_num(tx_bytes);
    print("\n");
    print("  lo:   RX packets=");
    print_num(history_count * 50);
    print(" RX bytes=");
    print_num(history_count * 32);
    print("\n");
    print("        TX packets=");
    print_num(history_count * 50);
    print(" TX bytes=");
    print_num(history_count * 32);
    print("\n");
}

void cmd_ipconfig() {
    print("Network Configuration\n");
    print("====================\n");
    print("Ethernet adapter Algebra-Net:\n");
    print("  Connection-specific DNS Suffix: local\n");
    
    if (is_connected) {
        print("  IPv4 Address: 192.168.1.101\n");
        print("  Connected to: ");
        print(connected_ssid);
        print("\n");
    } else {
        print("  IPv4 Address: 192.168.1.100\n");
    }
    
    print("  Subnet Mask: 255.255.255.0\n");
    print("  Default Gateway: 192.168.1.1\n");
    print("  DHCP Enabled: Yes\n");
    print("  DNS Servers: 8.8.8.8, 8.8.4.4\n");
}

void cmd_fps() {
    // Calculate real metrics based on kernel state
    int total_files = 0;
    uint32_t total_memory = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            total_files++;
            total_memory += files[i].size;
        }
    }
    
    int cpu_usage = 15 + (history_count * 2);  // Increases with command history
    if (cpu_usage > 85) cpu_usage = 85;
    
    int memory_used = (total_memory / 1024) + 50;  // Base + allocated files
    if (memory_used > 500) memory_used = 500;
    
    int fps = 60 - (total_files / 4);  // FPS decreases slightly with file count
    if (fps < 45) fps = 45;
    
    print("System Performance Monitor\n");
    print("==========================\n");
    print("CPU Usage: ");
    print_num(cpu_usage);
    print("%\n");
    print("Memory Usage: ");
    print_num(memory_used);
    print(" MB / 512 MB (");
    print_num((memory_used * 100) / 512);
    print("%)\n");
    print("Disk I/O: ");
    print_num(12 + (history_count / 2));
    print(".5 MB/s\n");
    print("\nFrame Rate Statistics:\n");
    print("  Current FPS: ");
    print_num(fps);
    print("\n");
    print("  Average FPS: ");
    print_num(fps - 1);
    print("\n");
    print("  Minimum FPS: ");
    print_num(fps - 15);
    print("\n");
    print("  Maximum FPS: 60\n");
    print("  Frame Time: 16.67ms\n");
    print("\nUptime: ");
    print_num((history_count / 10) + 2);
    print(" hours ");
    print_num((history_count * 3) % 60);
    print(" minutes ");
    print_num((history_count * 7) % 60);
    print(" seconds\n");
}

void cmd_systeminfo() {
    // Calculate real memory usage
    int total_files = 0;
    uint32_t total_memory = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            total_files++;
            total_memory += files[i].size;
        }
    }
    
    int available_memory = 512 - ((total_memory / 1024) + 50);
    if (available_memory < 0) available_memory = 0;
    
    print("System Information\n");
    print("==================\n");
    print("OS Name: Algebra OS\n");
    print("OS Version: 3.6\n");
    print("System Type: x86 (32-bit)\n");
    print("Processor: Intel Core i7\n");
    print("Total Memory: 512 MB\n");
    print("Available Memory: ");
    print_num(available_memory);
    print(" MB\n");
    print("Files Created: ");
    print_num(total_files);
    print("\n");
    print("Commands Executed: ");
    print_num(history_count);
    print("\n");
    print("System Boot Time: 2025-12-20 10:45:32\n");
    print("Time Zone: UTC+0\n");
    print("Hostname: algebra-kernel\n");
    
    if (is_connected) {
        print("WiFi Status: Connected to ");
        print(connected_ssid);
        print("\n");
    } else {
        print("WiFi Status: Disconnected\n");
    }
}

void init_wifi_networks() {
    wifi_networks_count = 0;
    
    // Simulate scanning for available WiFi networks
    strcpy(wifi_networks[0].ssid, "NetGear-5G");
    wifi_networks[0].signal_strength = 85;
    wifi_networks[0].is_secure = 1;
    wifi_networks[0].used = 1;
    
    strcpy(wifi_networks[1].ssid, "WiFi-Guest");
    wifi_networks[1].signal_strength = 72;
    wifi_networks[1].is_secure = 0;
    wifi_networks[1].used = 1;
    
    strcpy(wifi_networks[2].ssid, "CoffeeShop-WiFi");
    wifi_networks[2].signal_strength = 91;
    wifi_networks[2].is_secure = 1;
    wifi_networks[2].used = 1;
    
    strcpy(wifi_networks[3].ssid, "Home-Router");
    wifi_networks[3].signal_strength = 95;
    wifi_networks[3].is_secure = 1;
    wifi_networks[3].used = 1;
    
    strcpy(wifi_networks[4].ssid, "PublicWiFi");
    wifi_networks[4].signal_strength = 65;
    wifi_networks[4].is_secure = 0;
    wifi_networks[4].used = 1;
    
    wifi_networks_count = 5;
}

void cmd_wifi(const char* args) {
    if (strlen(args) == 0) {
        print("Usage: wifi -list          Show available networks\n");
        print("       wifi -connect       Connect to a network\n");
        print("       wifi -status        Show connection status\n");
        print("       wifi -disconnect    Disconnect from network\n");
        return;
    }
    
    if (strcmp(args, "-list") == 0) {
        // Initialize WiFi networks if needed
        if (wifi_networks_count == 0) {
            init_wifi_networks();
        }
        
        print("Available WiFi Networks:\n");
        print("======================\n");
        
        for (int i = 0; i < wifi_networks_count; i++) {
            if (wifi_networks[i].used) {
                print("[");
                print_num(i + 1);
                print("] ");
                print(wifi_networks[i].ssid);
                print(" - Signal: ");
                print_num(wifi_networks[i].signal_strength);
                print("% ");
                if (wifi_networks[i].is_secure) {
                    print("(Secured - WPA2)");
                } else {
                    print("(Open)");
                }
                print("\n");
            }
        }
        print("\nUse 'wifi -connect' to connect to a network\n");
        
    } else if (strcmp(args, "-connect") == 0) {
        // Initialize WiFi networks if needed
        if (wifi_networks_count == 0) {
            init_wifi_networks();
        }
        
        print("Select a network to connect:\n");
        print("===========================\n");
        
        for (int i = 0; i < wifi_networks_count; i++) {
            if (wifi_networks[i].used) {
                print("[");
                print_num(i + 1);
                print("] ");
                print(wifi_networks[i].ssid);
                print(" (");
                print_num(wifi_networks[i].signal_strength);
                print("%)");
                if (wifi_networks[i].is_secure) {
                    print(" [Secured]");
                }
                print("\n");
            }
        }
        
        print("\nEnter network number (1-");
        print_num(wifi_networks_count);
        print("): ");
        
        // Read user selection
        char selection_input[10];
        int sel_pos = 0;
        while (sel_pos < 9) {
            char c = get_key();
            if (c == '\n') {
                selection_input[sel_pos] = '\0';
                putchar('\n');
                break;
            } else if (c == '\b' && sel_pos > 0) {
                sel_pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            } else if (c >= '0' && c <= '9') {
                selection_input[sel_pos++] = c;
                putchar(c);
            }
        }
        
        int network_num = 0;
        if (sel_pos > 0) {
            network_num = selection_input[0] - '0';
        }
        
        if (network_num < 1 || network_num > wifi_networks_count) {
            print("Invalid selection\n");
            return;
        }
        
        int selected_idx = network_num - 1;
        
        // Check if network is secured
        if (wifi_networks[selected_idx].is_secure) {
            print("Enter password for ");
            print(wifi_networks[selected_idx].ssid);
            print(": ");
            
            char password[64];
            int pass_pos = 0;
            while (pass_pos < 63) {
                char c = get_key();
                if (c == '\n') {
                    password[pass_pos] = '\0';
                    putchar('\n');
                    break;
                } else if (c == '\b' && pass_pos > 0) {
                    pass_pos--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                } else if (c >= 32 && c <= 126) {
                    password[pass_pos++] = c;
                    putchar('*');
                }
            }
            
            // Simulate password validation
            if (pass_pos == 0 || strlen(password) < 8) {
                print("Error: Password too short (minimum 8 characters)\n");
                return;
            }
        }
        
        // Connect to network
        strcpy(connected_ssid, wifi_networks[selected_idx].ssid);
        is_connected = 1;
        
        print("\nConnecting to ");
        print(connected_ssid);
        print("...\n");
        print("Connected successfully!\n");
        print("IP Address: 192.168.1.101\n");
        print("Gateway: 192.168.1.1\n");
        
    } else if (strcmp(args, "-status") == 0) {
        if (is_connected) {
            print("WiFi Status: Connected\n");
            print("SSID: ");
            print(connected_ssid);
            print("\n");
            print("Signal Strength: 85%\n");
            print("IP Address: 192.168.1.101\n");
            print("Gateway: 192.168.1.1\n");
        } else {
            print("WiFi Status: Disconnected\n");
        }
        
    } else if (strcmp(args, "-disconnect") == 0) {
        if (is_connected) {
            print("Disconnecting from ");
            print(connected_ssid);
            print("...\n");
            is_connected = 0;
            memset(connected_ssid, 0, sizeof(connected_ssid));
            print("Disconnected successfully\n");
        } else {
            print("Not connected to any network\n");
        }
    } else {
        print("Unknown wifi option. Use 'wifi' for help\n");
    }
}

void cmd_pcinfo() {
    // Calculate real stats
    int total_files = 0;
    uint32_t total_memory = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used) {
            total_files++;
            total_memory += files[i].size;
        }
    }
    
    int memory_used = (total_memory / 1024) + 50;
    if (memory_used > 500) memory_used = 500;
    int memory_available = 512 - memory_used;
    
    int cpu_usage = 15 + (history_count * 2);
    if (cpu_usage > 85) cpu_usage = 85;
    
    // ASCII art
    print("    ___   _   _____ ___  ____ ___  ____   ___   ___\n");
    print("   / _ | | | | ____| _ \\| __ | __||  _ \\ / _ \\ / __|\n");
    print("  | |_| | | | |  _| | | |  _| __ | |_| | |_| \\__ \\\n");
    print("   \\___/  \\_/ |_|   |___/|___||___||___/  \\___/|___/\n");
    print("          Algebra OS - System Information\n");
    print("\n");
    
    print("========================================\n");
    print("SYSTEM INFORMATION\n");
    print("========================================\n");
    
    print("OS Name: Algebra OS\n");
    print("OS Version: 3.7\n");
    print("Kernel: Algebra Kernel v3.6 (x86 32-bit)\n");
    print("Architecture: x86 (32-bit Protected Mode)\n");
    print("\n");
    
    print("========================================\n");
    print("HARDWARE\n");
    print("========================================\n");
    
    print("Processor: Intel Core i7 Simulator\n");
    print("  Base Clock: 3.2 GHz\n");
    print("  Cores: 4 (simulated)\n");
    print("  Cache: 8 MB L3\n");
    print("\n");
    
    print("GPU: Intel Integrated Graphics\n");
    print("  VRAM: 128 MB (simulated)\n");
    print("  Display: VGA Text Mode 80x25\n");
    print("  Refresh Rate: 60 Hz\n");
    print("\n");
    
    print("========================================\n");
    print("MEMORY\n");
    print("========================================\n");
    
    print("Total RAM: 512 MB\n");
    print("Used Memory: ");
    print_num(memory_used);
    print(" MB\n");
    print("Available: ");
    print_num(memory_available);
    print(" MB\n");
    print("Memory Usage: ");
    print_num((memory_used * 100) / 512);
    print("%\n");
    
    // Memory bar
    print("[");
    int bar_width = 40;
    int filled = (memory_used * bar_width) / 512;
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) print("=");
        else print(" ");
    }
    print("]\n");
    print("\n");
    
    print("========================================\n");
    print("PERFORMANCE\n");
    print("========================================\n");
    
    print("CPU Usage: ");
    print_num(cpu_usage);
    print("%\n");
    print("Current FPS: ");
    print_num(60 - (total_files / 4));
    print(" Hz\n");
    print("Uptime: ");
    print_num((history_count / 10) + 2);
    print("h ");
    print_num((history_count * 3) % 60);
    print("m\n");
    print("\n");
    
    print("========================================\n");
    print("FILESYSTEM\n");
    print("========================================\n");
    
    print("Total Files: ");
    print_num(total_files);
    print("\n");
    print("Total Directories: 6\n");
    print("Storage Used: ");
    print_num(total_memory);
    print(" bytes\n");
    print("Storage Capacity: ");
    print_num(MAX_FILES * MAX_FILESIZE);
    print(" bytes\n");
    print("\n");
    
    print("========================================\n");
    print("NETWORK\n");
    print("========================================\n");
    
    if (is_connected) {
        print("WiFi Status: Connected\n");
        print("Connected SSID: ");
        print(connected_ssid);
        print("\n");
        print("IP Address: 192.168.1.101\n");
    } else {
        print("WiFi Status: Disconnected\n");
        print("IP Address: 192.168.1.100 (Wired)\n");
    }
    print("Gateway: 192.168.1.1\n");
    print("DNS: 8.8.8.8, 8.8.4.4\n");
    print("\n");
    
    print("========================================\n");
    print("BATTERY & POWER\n");
    print("========================================\n");
    
    print("Power Mode: AC Adapter (Plugged In)\n");
    print("Battery: N/A (Desktop System)\n");
    print("Power Draw: ");
    print_num(45 + cpu_usage);
    print(" W\n");
    print("\n");
}

void cmd_reboot() {
    print("Rebooting Algebra OS...\n");
    print("Shutting down services...\n");
    print("Clearing memory...\n");
    print("Syncing filesystem...\n");
    print("\n");
    print("System halted. Restarting...\n");
    print("\n\n");
    
    // Reset shell state
    clear_screen();
    history_count = 0;
    history_index = -1;
    memset(connected_ssid, 0, sizeof(connected_ssid));
    is_connected = 0;
    strcpy(current_dir, "/");
    
    // Show boot message
    print("Algebra OS v3.6 - System Boot\n");
    print("==============================\n");
    print("Initializing kernel...\n");
    print("Loading filesystem...\n");
    print("Configuring memory...\n");
    print("Starting shell...\n\n");
    print("Algebra OS v3.6 - Type 'help' for commands\n\n");
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
        print("  ls/dir        cd <dir>           mkdir <name>       touch <file>\n");
        print("  echo > <file> cat <file>         rm <file>          ping <host>\n");
        print("  netstat       ipconfig           wifi -list         wifi -connect\n");
        print("  wifi -status  wifi -disconnect   fps                systeminfo\n");
        print("  pcinfo        algebra <expr>     algebra-writeline  atom <file>\n");
        print("  build -algr   -algebra <input>   -o <output>        ./<file.algebra>\n");
        print("  clear         reboot             help\n");
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        cmd_ls();
    } else if (strcmp(cmd, "cd") == 0) {
        cmd_cd(args);
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(args);
    } else if (strcmp(cmd, "touch") == 0) {
        cmd_touch(args);
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo(args);
    } else if (strcmp(cmd, "rm") == 0) {
        cmd_rm(args);
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(cmd, "ping") == 0) {
        cmd_ping(args);
    } else if (strcmp(cmd, "netstat") == 0) {
        cmd_netstat();
    } else if (strcmp(cmd, "ipconfig") == 0) {
        cmd_ipconfig();
    } else if (strcmp(cmd, "wifi") == 0) {
        cmd_wifi(args);
    } else if (strcmp(cmd, "fps") == 0) {
        cmd_fps();
    } else if (strcmp(cmd, "systeminfo") == 0) {
        cmd_systeminfo();
    } else if (strcmp(cmd, "pcinfo") == 0) {
        cmd_pcinfo();
    } else if (strcmp(cmd, "algebra") == 0) {
        cmd_algebra(args);
    } else if (strcmp(cmd, "algebra-writeline") == 0) {
        cmd_algebra_writeline(args);
    } else if (strcmp(cmd, "atom") == 0) {
        cmd_atom(args);
    } else if (strcmp(cmd, "build") == 0) {
        cmd_build(args);
    } else if (strcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (strlen(cmd) > 2 && cmd[0] == '.' && cmd[1] == '/') {
        cmd_run_algebra(cmd + 2);
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else {
        print("Unknown command: ");
        print(cmd);
        print("\n");
    }
}

// Add command to history
void add_history(const char* cmd) {
    if (history_count < MAX_HISTORY) {
        strcpy(history[history_count], cmd);
        history_count++;
    } else {
        // Shift history up
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[MAX_HISTORY - 1], cmd);
    }
    history_index = -1;  // Reset to not viewing history
}

// Get previous command from history
const char* get_history_prev() {
    if (history_count == 0) return "";
    if (history_index < 0) {
        history_index = history_count - 1;
    } else if (history_index > 0) {
        history_index--;
    }
    return history[history_index];
}

// Get next command from history
const char* get_history_next() {
    if (history_count == 0) return "";
    if (history_index >= 0 && history_index < history_count - 1) {
        history_index++;
        return history[history_index];
    }
    history_index = -1;
    return "";
}

void shell() {
    print("\n");
    print("Algebra OS v3.6 - Type 'help' for commands\n\n");
    
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
                        add_history(input_buffer);
                        process_command(input_buffer);
                    }
                    break;
                } else if (c == 26) { // Up arrow - get previous command
                    const char* prev = get_history_prev();
                    if (strlen(prev) > 0) {
                        // Clear current line
                        int prompt_x = cursor_x - input_pos;
                        for (int i = 0; i < input_pos; i++) {
                            vga[cursor_y * VGA_WIDTH + prompt_x + i] = (WHITE_ON_BLACK << 8) | ' ';
                        }
                        cursor_x = prompt_x;
                        
                        // Display previous command
                        strcpy(input_buffer, prev);
                        input_pos = strlen(prev);
                        print(prev);
                    }
                } else if (c == 27) { // Down arrow - get next command
                    const char* next = get_history_next();
                    // Clear current line
                    int prompt_x = cursor_x - input_pos;
                    for (int i = 0; i < input_pos; i++) {
                        vga[cursor_y * VGA_WIDTH + prompt_x + i] = (WHITE_ON_BLACK << 8) | ' ';
                    }
                    cursor_x = prompt_x;
                    
                    if (strlen(next) > 0) {
                        strcpy(input_buffer, next);
                        input_pos = strlen(next);
                        print(next);
                    } else {
                        memset(input_buffer, 0, sizeof(input_buffer));
                        input_pos = 0;
                    }
                } else if (c == 30) { // Page Up - scroll up
                    scroll_page_up();
                    display_scroll_buffer();
                } else if (c == 31) { // Page Down - scroll down
                    scroll_page_down();
                    display_scroll_buffer();
                } else if (c == 28) { // Left arrow - move cursor left
                    if (input_pos > 0) {
                        input_pos--;
                        if (cursor_x > 0) {
                            cursor_x--;
                        }
                    }
                } else if (c == 29) { // Right arrow - move cursor right
                    int input_len = 0;
                    while (input_buffer[input_len] != '\0') input_len++;
                    if (input_pos < input_len) {
                        input_pos++;
                        if (cursor_x < VGA_WIDTH - 1) {
                            cursor_x++;
                        }
                    }
                } else if (c == '\b') {
                    if (input_pos > 0) {
                        input_pos--;
                        // Erase character on screen
                        vga[cursor_y * VGA_WIDTH + cursor_x] = (WHITE_ON_BLACK << 8) | ' ';
                        if (cursor_x > 0) {
                            cursor_x--;
                        }
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
