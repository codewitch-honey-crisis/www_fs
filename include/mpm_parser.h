#ifndef HTCW_MPM_PARSER_H
#define HTCW_MPM_PARSER_H
#include <stddef.h>

typedef enum {
    MPM_ERROR = -1,
    MPM_END = 0,
    MPM_HEADER_NAME_PART,
    MPM_HEADER_NAME_END,
    MPM_HEADER_VALUE_PART,
    MPM_HEADER_VALUE_END,
    MPM_CONTENT_PART,
    MPM_CONTENT_END    
} mpm_node_t;

typedef int(*mpm_on_read_callback)(void* state);
typedef struct {
    int state;
    const char* boundary;
    size_t boundary_size;
    int boundary_repl;
    int boundary_pos;
    int i;
    char skip_next_read;
    mpm_on_read_callback on_read;
    void* read_state;
} mpm_context_t;
#ifdef __cplusplus
extern "C" {
#endif
void mpm_init(const char* boundary, size_t boundary_size, mpm_on_read_callback on_read, void* read_state,mpm_context_t* out_context);
int mpm_init_file(const char* boundary, size_t boundary_size, const char* path, mpm_context_t* out_context);
mpm_node_t mpm_parse(mpm_context_t* ctx, void* buffer, size_t* in_out_size);
#ifdef __cplusplus
}
#endif
#endif // HTCW_MPM_PARSER_H
