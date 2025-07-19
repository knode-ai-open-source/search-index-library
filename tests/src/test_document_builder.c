# SPDX-FileCopyrightText: 2023-2025 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024-2025 Knode.ai
# SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "search-index-library/sil_document_builder.h"
#include "search-index-library/sil_document_image.h"
#include "a-memory-library/aml_pool.h"
#include "a-memory-library/aml_buffer.h"

int main() {
    // Initialize the document builder
    sil_document_builder_t *builder = sil_document_builder_init();
    if (!builder) {
        fprintf(stderr, "Failed to initialize document builder.\n");
        return EXIT_FAILURE;
    }

    // Add terms
    sil_document_builder_term(builder, "example");
    sil_document_builder_termf(builder, "term%d", 1);
    sil_document_builder_wterm(builder, 0, "wildcard_example");

    // Add terms with positions
    sil_document_builder_term_position(builder, 10, "positional_term");
    sil_document_builder_termf_position(builder, 20, "formatted_positional_term%d", 2);

    // Add terms with values
    sil_document_builder_term_value(builder, 42, "value_term");
    sil_document_builder_termf_value(builder, 100, "formatted_value_term%d", 3);

    // Add wildcard terms with positions
    sil_document_builder_wterm_position(builder, 50, 0, "wildcard_position_term");

    // Prepare dummy content and embeddings
    const char *content = "This is the document content.";
    uint32_t content_length = (uint32_t)strlen(content);
    int8_t embeddings[512] = {0};  // Example embeddings
    for( int i=0; i<512; i++ )
        embeddings[i] = (i % 255) - 127;
    uint32_t num_embeddings = 1;
    const void *dummy_data = embeddings;  // Placeholder for extra data
    uint32_t dummy_data_len = 64;

    aml_buffer_t *bh = aml_buffer_init(1024);

    // Generate the document image
    sil_document_builder_global(builder, bh, embeddings, num_embeddings, content, content_length, dummy_data, dummy_data_len);

    sil_document_image_t image;
    sil_document_image_init(&image, aml_buffer_data(bh), aml_buffer_length(bh));

    aml_pool_t *pool = aml_pool_init(1024);
    uint32_t num_terms;
    char **terms = sil_document_image_terms(pool, &image, &num_terms);
    for(uint32_t i=0; i<num_terms; i++) {
        printf("%s\n", terms[i]);
        sil_term_t *t = sil_document_image_term(&image, pool, terms[i]);
        if(!t) continue;
        sil_term_decode_positions(t);
        while(t->c.advance((atl_cursor_t *)t)) {
            sil_term_dump(t);
        }
        printf( "\n");
    }
    printf( "content: %s\n", image.content );
    aml_pool_destroy(pool);
    aml_buffer_destroy(bh);
    sil_document_builder_destroy(builder);

    return EXIT_SUCCESS;
}