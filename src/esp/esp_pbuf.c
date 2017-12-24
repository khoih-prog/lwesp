/**	
 * \file            esp_pbuf.c
 * \brief           Packet buffer manager
 */
 
/*
 * Copyright (c) 2017 Tilen Majerle
 *  
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software, 
 * and to permit persons to whom the Software is furnished to do so, 
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author:          Tilen MAJERLE <tilen@majerle.eu>
 */
#define ESP_INTERNAL
#include "include/esp_private.h"
#include "include/esp_pbuf.h"
#include "include/esp_mem.h"

#define SIZEOF_PBUF_STRUCT          ESP_MEM_ALIGN(sizeof(esp_pbuf_t))

/**
 * \brief           Skip pbufs for desired offset
 * \param[in]       p: Source pbuf to skip
 * \param[in]       off: Offset in units of bytes to skip
 * \param[out]      new_off: New offset on new returned pbuf
 * \return          New pbuf where offset was found or NULL if offset too big for pbuf chain
 */
static esp_pbuf_p
pbuf_skip(esp_pbuf_p p, size_t off, size_t* new_off) {
    if (p == NULL || p->tot_len < off) {        /* Check valid parameters */
        if (new_off != NULL) {                  /* Check new offset */
            *new_off = 0;                       /* Set offset to output variable */
        }
        return NULL;
    }
    
    /**
     * Skip pbufs until we reach pbuf where offset is placed
     */
    for (; p != NULL && p->len <= off; p = p->next) {
        off -= p->len;                          /* Decrease offset by current pbuf length */
    }
    
    if (new_off != NULL) {                      /* Check new offset */
        *new_off = off;                         /* Set offset to output variable */
    }
    return p;
}

/**
 * \brief           Allocate packet buffer for network data of specific size
 * \param[in]       len: Length of payload memory to allocate
 * \return          Pointer to allocated memory or NULL in case of failure
 */
esp_pbuf_p
esp_pbuf_new(size_t len) {
    esp_pbuf_p p;
    
    p = esp_mem_calloc(1, SIZEOF_PBUF_STRUCT + len);    /* Allocate memory for packet buffer */
    ESP_DEBUGW(ESP_DBG_PBUF, p == NULL, "PBUF: Failed to allocate %d bytes\r\n", (int)len);
    ESP_DEBUGW(ESP_DBG_PBUF, p != NULL, "PBUF: Allocated %d bytes on %p\r\n", (int)len, p);
    if (p != NULL) {
        p->next = NULL;                         /* No next element in chain */
        p->tot_len = len;                       /* Set total length of pbuf chain */
        p->len = len;                           /* Set payload length */
        p->payload = (uint8_t *)(((char *)p) + SIZEOF_PBUF_STRUCT); /* Set pointer to payload data */
        p->ref = 1;                             /* Single reference is used on this pbuf */
    }
    return p;
}

/**
 * \brief           Free previously allocated packet buffer
 * \param[in]       pbuf: Packet buffer to free
 * \return          Number of freed pbufs from head
 */
uint16_t
esp_pbuf_free(esp_pbuf_p pbuf) {
    esp_pbuf_p p, pn;
    uint16_t ref, cnt;
    
    ESP_ASSERT("pbuf != NULL", pbuf != NULL);   /* Assert input parameters */
    
    /*
     * Free all pbufs until first ->ref > 1 is reached
     * which means somebody has reference to part of pbuf and we have to keep it as is
     */
    cnt = 0;
    for (p = pbuf; p != NULL;) {
        ref = --p->ref;                         /* Decrease current value and save it */
        if (ref == 0) {                         /* Did we reach 0 and are ready to free it? */
            ESP_DEBUGF(ESP_DBG_PBUF, "PBUF: Deallocating %p with len/tot_len: %d/%d\r\n", p, (int)p->len, (int)p->tot_len);
            pn = p->next;                       /* Save next entry */
            esp_mem_free(p);                    /* Free memory for pbuf */
            p = pn;                             /* Restore with next entry */
            cnt++;                              /* Increase number of freed pbufs */
        } else {
            break;
        }
    }
    return cnt;
}

/**
 * \brief           Contencate 2 packet buffers together to one big packet
 * \note            After tail pbuf has been added to head pbuf chain,
 *                  it must not be referenced by user anymore as it is now completelly controlled by head pbuf.
 *                  In simple words, when user calls this function, it should not call esp_pbuf_free function anymore,
 *                  as it might make memory undefined for head pbuf.
 * \param[in]       head: Head packet buffer to append new pbuf to
 * \param[in]       tail: Tail packet buffer to append to head pbuf
 * \return          espOK on success, member of \ref espr_t otherwise
 * \sa              esp_pbuf_chain
 */
espr_t
esp_pbuf_cat(esp_pbuf_p head, esp_pbuf_p tail) {
    ESP_ASSERT("head != NULL", head != NULL);   /* Assert input parameters */
    ESP_ASSERT("tail != NULL", tail != NULL);   /* Assert input parameters */
    
    /*
     * For all pbuf packets in head,
     * increase total length parameter of all next entries
     */
    for (; head->next != NULL; head = head->next) {
        head->tot_len += tail->tot_len;         /* Increase total length of packet */
    }
    head->tot_len += tail->tot_len;             /* Increase total length of last packet in chain */
    head->next = tail;                          /* Set next packet buffer as next one */
    
    return espOK;
}

/**
 * \brief           Chain 2 pbufs together. Similar to \ref esp_pbuf_cat
 *                  but now new reference is done from head pbuf to tail pbuf.
 * \note            After this function call, user must call \ref esp_pbuf_free(tail) to remove
 *                  its reference to tail pbuf and allow control to head pbuf
 * \param[in]       head: Head packet buffer to append new pbuf to
 * \param[in]       tail: Tail packet buffer to append to head pbuf
 * \return          espOK on success, member of \ref espr_t otherwise
 * \sa              esp_pbuf_cat
 */
espr_t
esp_pbuf_chain(esp_pbuf_p head, esp_pbuf_p tail) {
    espr_t res;
    
    /*
     * First contencate them together
     * Second create a new reference from head buffer to tail buffer
     * so user can normally use tail pbuf and free it when it wants    
     */
    if ((res = esp_pbuf_cat(head, tail)) == espOK) {    /* Did we contencate them together successfully? */
        esp_pbuf_ref(tail);                     /* Reference tail pbuf by head pbuf now */
    }
    return res;
}

/**
 * \brief           Increment reference count on pbuf
 * \param[in]       pbuf: pbuf to increase reference
 * \return          espOK on success, member of \ref espr_t otherwise
 */
espr_t
esp_pbuf_ref(esp_pbuf_p pbuf) {
    ESP_ASSERT("pbuf != NULL", pbuf != NULL);   /* Assert input parameters */
    
    pbuf->ref++;                                /* Increase reference count for pbuf */
    return espOK;
}

/**
 * \brief           Copy user data to chain of pbufs
 * \param[in]       pbuf: First pbuf in chain to start copying to
 * \param[in]       data: Input data to copy to pbuf memory
 * \param[in]       len: Length of input data to copy
 * \param[in]       offset: Start offset in pbuf where to start copying
 * \return          espOK on success, member of \ref espr_t otherwise
 */
espr_t
esp_pbuf_take(esp_pbuf_p pbuf, const void* data, size_t len, size_t offset) {
    const uint8_t* d = data;
    size_t copy_len;
    
    ESP_ASSERT("pbuf != NULL", pbuf != NULL);   /* Assert input parameters */
    ESP_ASSERT("data != NULL", data != NULL);   /* Assert input parameters */
    ESP_ASSERT("len > 0", len > 0);             /* Assert input parameters */
    ESP_ASSERT("pbuf->tot_len >= len", pbuf->tot_len >= len);   /* Assert input parameters */
    
    /*
     * Skip if necessary and check if we are in valid range
     */
    if (offset) {
        pbuf = pbuf_skip(pbuf, offset, &offset);    /* Offset and check for new length */
        if (pbuf == NULL) {
            return espERR;
        }
    }
    
    if (pbuf->tot_len < (len + offset)) {
        return espPARERR;
    }
    
    /*
     * First only copy in case we have some offset from first pbuf
     */
    if (offset) {
        size_t l = ESP_MIN(pbuf->len - offset, len);    /* Get length to copy to current pbuf */
        memcpy(pbuf->payload + offset, d, l);   /* Copy to memory with offset */
        len -= l;                               /* Decrease remaining bytes to copy */
        d += l;                                 /* Increase data pointer */
        pbuf = pbuf->next;                      /* Go to next pbuf */
    }
    
    /*
     * Copy user memory to sequence of pbufs
     */
    for (; len; pbuf = pbuf->next) {
        copy_len = len > pbuf->len ? pbuf->len : len;   /* Get copy length */
        memcpy(pbuf->payload, d, copy_len);     /* Copy memory to pbuf payload */
        len -= copy_len;                        /* Decrease number of remaining bytes to send */
        d += copy_len;                          /* Increase data pointer */
    }
    return espOK;
}

/**
 * \brief           Copy memory from pbuf to user linear memory
 * \param[in]       pbuf: Pbuf to copy from
 * \param[out]      data: User linear memory to copy to
 * \param[in]       len: Length of data in units of bytes
 * \param[in]       offset: Possible start offset in pbuf
 * \return          Number of bytes copied
 */
size_t
esp_pbuf_copy(esp_pbuf_p pbuf, void* data, size_t len, size_t offset) {
    size_t tot, tc;
    uint8_t* d = data;
    
    if (pbuf == NULL || data == NULL || !len || pbuf->tot_len < offset) {   /* Assert input parameters */
        return 0;
    }
    
    /*
     * In case user wants offset, 
     * skip to necessary pbuf
     */
    if (offset) {
        pbuf = pbuf_skip(pbuf, offset, &offset);    /* Skip offset if necessary */
        if (pbuf == NULL) {
            return 0;
        }
    }
    
    /*
     * Copy data from pbufs to memory
     * with checking for initial offset (only one can have offset)
     */
    tot = 0;
    for (; pbuf != NULL && len; pbuf = pbuf->next) {
        tc = ESP_MIN(pbuf->len - offset, len);      /* Get length of data to copy */
        memcpy(d, pbuf->payload + offset, tc);      /* Copy data from pbuf */
        d += tc;
        len -= tc;
        tot += tc;
        offset = 0;                                 /* No more offset in this case */
    }
    return tot;
}

/**
 * \brief           Get value from pbuf at specific position
 * \param[in]       pbuf: Pbuf used to get data from
 * \param[in]       pos: Position at which to get element
 * \param[out]      el: Output variable to save element value at desired position
 * \return          1 on success, 0 otherwise
 */
uint8_t
esp_pbuf_get_at(const esp_pbuf_p pbuf, size_t pos, uint8_t* el) {
    esp_pbuf_p p;
    size_t off;
    
    if (pbuf != NULL) {
        p = pbuf_skip(pbuf, pos, &off);         /* Skip pbufs to desired position and get new offset from new pbuf */
        if (p != NULL) {                        /* Do we have new pbuf? */
            *el = p->payload[off];              /* Return memory at desired new offset from latest pbuf */
            return 1;
        }
    }
    return 0;                                   /* Invalid character */
}

/**
 * \brief           Find desired needle in a haystack
 * \param[in]       pbuf: Pbuf used as haystack
 * \param[in]       needle: Data memory used as needle
 * \param[in]       len: Length of needle memory
 * \param[in]       off: Starting offset in pbuf memory
 * \return          ESP_SIZET_MAX if no match or position where in pbuf we have a match
 * \sa              esp_pbuf_strfind
 */
size_t
esp_pbuf_memfind(const esp_pbuf_p pbuf, const void* needle, size_t len, size_t off) {
    size_t i;
    if (pbuf != NULL && needle != NULL && pbuf->tot_len >= (len + off)) {   /* Check if valid entries */
        /*
         * Try entire buffer element by element
         * and in case we have a match, report it
         */
        for (i = off; i <= pbuf->tot_len - len; i++) {
            if (!esp_pbuf_memcmp(pbuf, needle, len, i)) {   /* Check if identical */
                return i;                       /* We have a match! */
            }
        }
    }
    return ESP_SIZET_MAX;                       /* Return maximal value of size_t variable to indicate error */
}

/**
 * \brief           Find desired needle (str) in a haystack (pbuf)
 * \param[in]       pbuf: Pbuf used as haystack
 * \param[in]       str: String to search for in pbuf
 * \param[in]       off: Starting offset in pbuf memory
 * \return          ESP_SIZET_MAX if no match or position where in pbuf we have a match
 * \sa              esp_pbuf_memfind
 */
size_t
esp_pbuf_strfind(const esp_pbuf_p pbuf, const char* str, size_t off) {
    return esp_pbuf_memfind(pbuf, str, strlen(str), off);
}

/**
 * \brief           Compare pbuf memory with memory from data
 * \note            Compare is done on entire pbuf chain
 * \param[in]       pbuf: Pbuf used to compare with data memory
 * \param[in]       data: Actual data to compare with
 * \param[in]       len: Length of input data in units of bytes
 * \param[in]       offset: Start offset to use when comparing data
 * \return          0 if equal, ESP_SIZET_MAX if memory/offset too big or anything between if not equal
 * \sa              esp_pbuf_strcmp
 */
size_t
esp_pbuf_memcmp(const esp_pbuf_p pbuf, const void* data, size_t len, size_t offset) {
    esp_pbuf_p p;
    size_t i;
    uint8_t el;
    const uint8_t* d = data;
    
    if (pbuf == NULL || data == NULL || !len || /* Input parameters check */
        pbuf->tot_len < (offset + len)) {       /* Check of valid ranges */
        return ESP_SIZET_MAX;                   /* Invalid check here */
    }
    
    /*
     * Find start pbuf to have more optimized search at the end
     * Since we had a check on beginning, we must pass this for loop without any problems
     */
    for (p = pbuf; p != NULL && p->len <= offset; p = p->next) {
        offset -= p->len;                       /* Decrease offset by length of pbuf */
    }
    
    /*
     * We have known starting pbuf.
     * Now it is time to check byte by byte from pbuf and memory
     *
     * Use byte by byte read function to inspect bytes separatelly
     */
    for (i = 0; i < len; i++) {
        if (!esp_pbuf_get_at(p, offset + i, &el) || el != d[i]) {   /* Get value from pbuf at specific offset */
            return offset + 1;                  /* Return value from offset where it failed */
        }
    }
    return 0;                                   /* Memory matches at this point */
}

/**
 * \brief           Compare pbuf memory with input string
 * \note            Compare is done on entire pbuf chain
 * \param[in]       pbuf: Pbuf used to compare with data memory
 * \param[in]       str: String to be compared with pbuf
 * \param[in]       offset: Start memory offset in pbuf
 * \return          0 if equal, ESP_SIZET_MAX if memory/offset too big or anything between if not equal
 * \sa              esp_pbuf_memcmp
 */
size_t
esp_pbuf_strcmp(const esp_pbuf_p pbuf, const char* str, size_t offset) {
    return esp_pbuf_memcmp(pbuf, str, strlen(str), offset);
}

/**
 * \brief           Get linear offset address for pbuf from specific offset
 * \note            Since pbuf memory can be fragmentized in chain,
 *                  you may need to call function multiple times to get memory for entire pbuf at different
 * \param[in]       pbuf: Pbuf to get linear address
 * \param[in]       offset: Start offset from where to start
 * \param[out]      new_len: Length of memory returned by function
 * \return          Pointer to memory on success, NULL on failure
 */
const void *
esp_pbuf_get_linear_addr(const esp_pbuf_p pbuf, size_t offset, size_t* new_len) {
    esp_pbuf_p p = pbuf;
    if (pbuf == NULL || pbuf->tot_len < offset) {   /* Check input parameters */
        if (new_len != NULL) {
            *new_len = 0;
        }
        return NULL;
    }
    if (offset) {                               /* Is there any offset? */
        p = pbuf_skip(pbuf, offset, &offset);   /* Skip pbuf to desired length */
        if (p == NULL) {
            if (new_len != NULL) {
                *new_len = 0;
            }
            return NULL;
        }
    }
    if (new_len != NULL) {
        *new_len = p->len - offset;             /* Save memory length user can use */
    }
    return &p->payload[offset];                 /* Return memory at desired offset */
}

/**
 * \brief           Get data pointer from packet buffer
 * \param[in]       pbuf: Packet buffer
 * \return          Pointer to data buffer or NULL if invalid buffer
 */
const void *
esp_pbuf_data(const esp_pbuf_p pbuf) {
    return pbuf != NULL ? pbuf->payload : NULL;
}

/**
 * \brief           Get length of packet buffer
 * \param[in]       pbuf: Packet buffer to get length for
 * \param[in]       tot: Set to 1 to return total packet chain length or 0 to get only first packet length
 * \return          Length of data in units of bytes
 */
size_t
esp_pbuf_length(const esp_pbuf_p pbuf, uint8_t tot) {
    return pbuf != NULL ? (tot ? pbuf->tot_len : pbuf->len) : 0;
}

/**
 * \brief           Set IP address and port number for received data
 * \param[in]       pbuf: Packet buffer
 * \param[in]       ip: IP to assing to packet buffer
 * \param[in]       port: Port number to assign to packet buffer
 */
void
esp_pbuf_set_ip(esp_pbuf_p pbuf, const void* ip, uint16_t port) {
    if (pbuf != NULL && ip != NULL) {
        memcpy(pbuf->ip, ip, 4);
        pbuf->port = port;
    }
}

/**
 * \brief           Advance pbuf payload pointer by number of \arg len bytes
 *                  It can only advance single pbuf in a chain
 *
 * \note            When other pbufs are referencing current one, 
 *                  they are not adjusted in length and total length
 *
 * \param[in]       pbuf: Pbuf to advance
 * \param[in]       len: Number of bytes to advance, when negative is used, buffer is increased by only if it was decreased before
 * \return          1 on success, 0 otherwise
 */
uint8_t
esp_pbuf_advance(esp_pbuf_p pbuf, int len) {
    uint8_t process = 0;
    if (pbuf == NULL || !len) {
        return 0;
    }
    if (len > 0) {                              /* When we want to decrease size */
        if (len <= pbuf->len) {                 /* Is there space to decrease? */
            process = 1;
        }
    } else {
        /* Is current payload + new len still higher than pbuf structure? */
        if (((uint8_t *)pbuf + SIZEOF_PBUF_STRUCT) < (pbuf->payload + len)) {
            process = 1;
        }
    }
    if (process) {
        pbuf->payload += len;                   /* Increase payload pointer */
        pbuf->tot_len -= len;                   /* Decrease length of pbuf chain */
        pbuf->len -= len;                       /* Decrease length of current pbuf */
    }
    return process;
}

/**
 * \brief           Skip a list of pbufs for desired offset
 * \note            Reference is not changed after return and user must not free the memory of new pbuf directly
 * \param[in]       pbuf: Start of pbuf chain
 * \param[in]       offset: Offset in units of bytes to skip
 * \param[out]      new_offset: Pointer to output variable to save new offset in returned pbuf
 * \return          New pbuf on success or NULL on failure
 */
esp_pbuf_p
esp_pbuf_skip(esp_pbuf_p pbuf, size_t offset, size_t* new_offset) {
    return pbuf_skip(pbuf, offset, new_offset); /* Skip pbufs with internal function */
}
