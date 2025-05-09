#include <stdio.h>
#include <string.h>
#include "mpm_parser.h"
typedef enum {
    MPM_S_ERROR = -1,
    MPM_S_END,
    MPM_S_INIT,
    MPM_S_INIT_BOUNDARY,
    MPM_S_INIT_CRLF_OR_END,
    MPM_S_CONTENT,
    MPM_S_CONTENT_THEN_HEADER,
    MPM_S_CONTENT_THEN_END,
    MPM_S_MAYBE_BOUNDARY,
    MPM_S_MAYBE_BOUNDARY_2,
    MPM_S_BOUNDARY,
    MPM_S_COPY_BOUNDARY_PART,
    MPM_S_HEADER,
    MPM_S_HEADER_NAME,
    MPM_S_HEADER_SEP,
    MPM_S_HEADER_VALUE,
    MPM_S_HEADER_VALUE_END
} mpm_state_t;
static void mpm_init_impl(const char* boundary, size_t boundary_size, mpm_on_read_callback on_read, void* read_state, mpm_context_t* out_context) {
    out_context->state = (int)MPM_S_INIT;
    out_context->boundary = boundary;
    out_context->boundary_size = boundary_size==0?strlen(boundary):boundary_size;
    out_context->skip_next_read = 0;
    out_context->on_read = on_read;
    out_context->read_state = read_state;
}
void mpm_init(const char* boundary, size_t boundary_size, mpm_on_read_callback on_read, void* read_state, mpm_context_t* out_context) {
    
    mpm_init_impl   (boundary,boundary_size ,on_read,read_state,out_context);
}
static int mpm_file_callback(void* state) {
    if(state==NULL) return -1;
    FILE* f = (FILE*)state;
    return fgetc(f);
}
int mpm_init_file(const char* boundary, size_t boundary_size, const char* path,mpm_context_t* out_context) {
    FILE* f = fopen(path,"rb");
    if(f==NULL) {
        return -1;
    }
    mpm_init_impl(boundary,boundary_size, mpm_file_callback, f,out_context);
    return 0;
}
mpm_node_t mpm_parse(mpm_context_t* ctx, void* buffer, size_t* in_out_size) {
    if(ctx->state==(int)MPM_S_END) {
        ctx->state = (int)MPM_S_END;
        return MPM_END;
    }
    if(ctx->state==(int)MPM_S_ERROR) {
        goto error;
    }
    size_t size = *in_out_size;
    *in_out_size = 0;
    if(!size) {
        return MPM_ERROR;
    }
    if(!ctx->skip_next_read) {
        ctx->i = ctx->on_read(ctx->read_state);
    }
    ctx->skip_next_read = 0;
    while(1) {
        switch(ctx->state) {
            case (int)MPM_S_END:
                return MPM_END;
            case (int)MPM_S_ERROR:
                return MPM_ERROR;
            case (int)MPM_S_INIT:
                ctx->boundary_pos = 0;
                if(ctx->i!='-') { goto error; }
                ctx->i = ctx->on_read(ctx->read_state);
                if(ctx->i!='-') { goto error; }
                ctx->i = ctx->on_read(ctx->read_state);
                ctx->state = (int)MPM_S_INIT_BOUNDARY;
                break;
            case (int)MPM_S_INIT_BOUNDARY:
                if(ctx->i!=ctx->boundary[ctx->boundary_pos]) {
                    goto error;
                }
                ++ctx->boundary_pos;
                if(ctx->boundary_pos==ctx->boundary_size) {
                    ctx->i = ctx->on_read(ctx->read_state);
                    ctx->state = (int)MPM_S_INIT_CRLF_OR_END;
                    break;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                break;
            case (int)MPM_S_INIT_CRLF_OR_END:
                if(ctx->i!='\r') {
                    goto error;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                if(ctx->i!='\n') {
                    goto error;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                ctx->state = (int)MPM_S_HEADER;
                ctx->boundary_pos = 0;
                break;
            case (int)MPM_S_HEADER:
                if(ctx->i=='\r') {
                    ctx->i = ctx->on_read(ctx->read_state);
                    if(ctx->i=='\n') {
                        ctx->i = ctx->on_read(ctx->read_state);
                        ctx->state = (int)MPM_S_CONTENT;
                        break;
                    }
                }
                ctx->state = (int)MPM_S_HEADER_NAME;
                break;
            case (int)MPM_S_HEADER_NAME: 
                if(ctx->i==':') {
                    ctx->state = (int)MPM_S_HEADER_SEP;
                    ctx->skip_next_read = 1;
                    return MPM_HEADER_NAME_END;
                }
                ((char*)buffer)[(*in_out_size)++]=ctx->i;
                --size;
                if(!size) {
                    return MPM_HEADER_NAME_PART;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                if(ctx->i==':') {
                    ctx->skip_next_read = 1;
                    return MPM_HEADER_NAME_PART;
                }
                break;
            case (int)MPM_S_HEADER_SEP:
                if(ctx->i!=':') {
                    goto error;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                while(' '==ctx->i) {
                    ctx->i = ctx->on_read(ctx->read_state);
                }
                ctx->state = (int)MPM_S_HEADER_VALUE;
                break;
            case (int)MPM_S_HEADER_VALUE:
                if(ctx->i=='\r') {
                    ctx->i = ctx->on_read(ctx->read_state);
                    if(ctx->i!='\n') {
                        goto error;
                    }
                    ctx->state = (int)MPM_S_HEADER_VALUE_END;
                    return MPM_HEADER_VALUE_END;
                }
                ((char*)buffer)[(*in_out_size)++]=ctx->i;
                --size;
                if(!size) {
                    return MPM_HEADER_VALUE_PART;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                if(ctx->i=='\r') {
                    ctx->skip_next_read = 1;
                    return MPM_HEADER_VALUE_PART;
                }
                break;
            case (int)MPM_S_HEADER_VALUE_END:
                if(ctx->i=='\r') {
                    ctx->i = ctx->on_read(ctx->read_state);
                    if(ctx->i!='\n') {
                        ((char*)buffer)[(*in_out_size)++]='\r';
                        --size;
                        ctx->state = (int)MPM_S_HEADER;
                        break;
                    }
                    ctx->state = (int)MPM_S_CONTENT;
                    ctx->i = ctx->on_read(ctx->read_state);
                    break;
                }
                ctx->state = (int)MPM_S_HEADER;
                break;
            case (int)MPM_S_CONTENT:
                if(ctx->i == '\r') {
                    ctx->boundary_pos = -4;
                    ctx->state = (int)MPM_S_MAYBE_BOUNDARY;
                    break;
                }
                ((char*)buffer)[(*in_out_size)++]=ctx->i;
                --size;
                if(!size) {
                    return MPM_CONTENT_PART;
                }
                ctx->i = ctx->on_read(ctx->read_state);
                break;
            case (int)MPM_S_CONTENT_THEN_HEADER:
                ctx->state = (int)MPM_S_HEADER;
                ctx->skip_next_read = 1;
                return MPM_CONTENT_END;
            case (int)MPM_S_CONTENT_THEN_END:
                ctx->state = (int)MPM_S_END;
                return MPM_CONTENT_END;
            
            case (int)MPM_S_MAYBE_BOUNDARY: 
                if(ctx->boundary_pos==ctx->boundary_size) {
                    if(ctx->i!='\r' && ctx->i!='-') {
                        ctx->boundary_pos = -4;
                        ctx->boundary_repl = ctx->boundary_size;
                        ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        break;
                    }
                    if(ctx->i=='-') {
                        ctx->state = (int)MPM_S_MAYBE_BOUNDARY_2;
                        break;
                    } else {
                        ++ctx->boundary_pos;
                        ctx->i = ctx->on_read(ctx->read_state);
                        break;
                    }
                } else if(ctx->boundary_pos==ctx->boundary_size+1) {
                    if(ctx->i!='\n') {
                        ctx->boundary_pos = -4;
                        ctx->boundary_repl = ctx->boundary_size+1;
                        ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        break;
                    }
                    ctx->state =  (int)MPM_S_HEADER;
                    ctx->i = ctx->on_read(ctx->read_state);
                    if(*in_out_size) {
                        ctx->skip_next_read = 1;
                        ctx->state = (int)MPM_S_CONTENT_THEN_HEADER;
                        return MPM_CONTENT_PART;
                    }
                    break;
                } 
                switch(ctx->boundary_pos) {
                    case -4:
                        if(ctx->i!='\r') {
                            ctx->boundary_pos = -4;
                            ctx->boundary_repl =-4;
                            ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        }
                        break;
                    case -3:
                        if(ctx->i!='\n') {
                            ctx->boundary_pos = -4;
                            ctx->boundary_repl =-3;
                            ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        }
                        break;
                    case -2:
                        if(ctx->i!='-') {
                            ctx->boundary_pos = -4;
                            ctx->boundary_repl =-2;
                            ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        }
                        break;
                    case -1:
                        if(ctx->i!='-') {
                            ctx->boundary_pos = -4;
                            ctx->boundary_repl =-1;
                            ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                        }
                        break;
                }
                if(ctx->state==(int)MPM_S_COPY_BOUNDARY_PART) {
                    break;
                }
                if(ctx->boundary_pos<0) {
                    ++ctx->boundary_pos;
                    ctx->i = ctx->on_read(ctx->read_state);
                    break;
                } 
                if(ctx->boundary[ctx->boundary_pos]!=ctx->i) {
                    ctx->boundary_repl =ctx->boundary_pos;
                    ctx->boundary_pos = -4;
                    ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                    break;
                }
                ++ctx->boundary_pos;
                ctx->i = ctx->on_read(ctx->read_state);
            
                break;
            case (int)MPM_S_MAYBE_BOUNDARY_2: 
                if(ctx->i!='-') {
                    ctx->boundary_pos = -4;
                    ctx->boundary_repl = ctx->boundary_size+1;
                    ctx->state = (int)MPM_S_COPY_BOUNDARY_PART;
                    break;
                }
                if(*in_out_size) {
                    ctx->state = (int)MPM_S_CONTENT_THEN_END;
                    return MPM_CONTENT_PART;
                }
                goto done;
            case (int)MPM_S_COPY_BOUNDARY_PART:
                if(ctx->boundary_pos>=ctx->boundary_repl) {
                    ctx->state = MPM_S_CONTENT;
                    break;
                }
                switch(ctx->boundary_pos) {
                    case -4:
                        ((char*)buffer)[(*in_out_size)++]='\r'; --size;
                        break;
                    case -3:
                        ((char*)buffer)[(*in_out_size)++]='\n'; --size;
                        break;
                    case -2:
                    case -1:
                        ((char*)buffer)[(*in_out_size)++]='-'; --size;
                        break;
                    default:
                        if(ctx->boundary_pos<ctx->boundary_size) {
                            ((char*)buffer)[(*in_out_size)++]=ctx->boundary[ctx->boundary_pos]; --size;
                        } else if(ctx->boundary_pos==ctx->boundary_size+1) {
                            if(ctx->i=='-') {
                                ((char*)buffer)[(*in_out_size)++]='-'; 
                            } else {
                                ((char*)buffer)[(*in_out_size)++]='\r'; 
                            }
                            --size;
                        } else if(ctx->boundary_pos==ctx->boundary_size+1) {
                            if(ctx->i=='-') {
                                ((char*)buffer)[(*in_out_size)++]='-'; 
                            } else {
                                ((char*)buffer)[(*in_out_size)++]='\n'; 
                            }
                            --size;
                        }
                        break;
                }
                ++ctx->boundary_pos;
                break;
        }
    }
error:
    ctx->state = (int)MPM_S_ERROR;
    return MPM_ERROR;
done:
    ctx->state = (int)MPM_S_END;    
    return MPM_END;
    
}