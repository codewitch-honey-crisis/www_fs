#ifndef HTTP_UTIL_H
#define HTTP_UTIL_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/// @brief Returns each/path/segment in the URI
/// @param out_segment the buffer to hold the next path segment
/// @param segment_size the size of the segment buffer
/// @param next_path_part the return value from the previous call
/// @return the next path part
const char* http_crack_path(char* out_segment, size_t segment_size, const char* next_path_part);
/// @brief Returns each name=value pair in the request query (which starts with ?)
/// @param out_name the buffer to hold the next argument name
/// @param name_size the size of the name buffer
/// @param out_value the buffer to hold the next argument value
/// @param value_size the value buffer
/// @param next_query_part the return value from the previous call
/// @return the next query part
const char* http_crack_query(char* out_name, size_t name_size, char* out_value, size_t value_size, const char* next_query_part);
/// @brief Encodes data in RFC3986 format
/// @param data The string to encode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The encoded string
char* http_url_encode_rfc3986(const char *data, char *out_buffer, size_t out_size);
/// @brief Encodes data in RFC3986 format
/// @param data The string to encode
/// @param size The number of characters to encode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The encoded string
char* http_url_encode_rfc3986_part(const char *data, size_t size, char *out_buffer, size_t out_size);
/// @brief Encodes data in HTML5 format
/// @param data The string to encode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The encoded string
char* http_url_encode_html5(const char *data, char *out_buffer, size_t out_size);
/// @brief Encodes data in HTML5 format
/// @param data The string to encode
/// @param size The number of characters to encode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The encoded string
char* http_url_encode_html5_part(const char *data, size_t size, char *out_buffer, size_t out_size);
/// @brief Decodes an url encoded string
/// @param data The string to decode
/// @param out_buffer The out buffer for the decoded string
/// @param out_size The size of the out buffer
/// @return The decoded string
char* http_url_decode(const char *data, char *out_buffer, size_t out_size);
/// @brief Decodes an url encoded string
/// @param data The string to decode
/// @param size The number of characters to decode
/// @param out_buffer The out buffer for the decoded string
/// @param out_size The size of the out buffer
/// @return The decoded string
char* http_url_decode_part(const char *data, size_t size, char *out_buffer, size_t out_size);
/// @brief Encodes a path in RFC3986 format
/// @param path The path to encode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The encoded path
char* http_url_encode_path(const char *path, char *out_buffer, size_t out_size);
/// @brief Decodes an URL encoded path
/// @param path The path to decode
/// @param out_buffer The out buffer for the encoded string
/// @param out_size The size of the out buffer
/// @return The decoded path
char* http_url_decode_path(const char *path, char *out_buffer, size_t out_size);

#ifdef __cplusplus
}
#endif
#endif // HTTP_UTIL_H