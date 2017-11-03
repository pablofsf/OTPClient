#include <glib.h>
#include <gio/gio.h>
#include <gcrypt.h>
#include <json-glib/json-glib.h>
#include "file-size.h"
#include "imports.h"

#define IV_SIZE 12
#define TAG_SIZE 16

guchar *get_sha256 (const gchar *password);

GSList *parse_json_data (const gchar *data);


GSList *
decrypt_json (const gchar *path, const gchar *password)
{
    GError *err = NULL;
    gcry_cipher_hd_t hd;

    goffset input_file_size = get_file_size (path);

    GFile *in_file = g_file_new_for_path (path);
    GFileInputStream *in_stream = g_file_read (in_file, NULL, &err);
    if (err != NULL) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_file);
        return NULL;
    }

    guchar iv[IV_SIZE];
    if (g_input_stream_read (G_INPUT_STREAM (in_stream), iv, IV_SIZE, NULL, &err) == -1) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_stream);
        g_object_unref (in_file);
        return NULL;
    }

    guchar tag[TAG_SIZE];
    if (!g_seekable_seek (G_SEEKABLE (in_stream), input_file_size - TAG_SIZE, G_SEEK_SET, NULL, &err)) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_stream);
        g_object_unref (in_file);
        return NULL;
    }
    if (g_input_stream_read (G_INPUT_STREAM (in_stream), tag, TAG_SIZE, NULL, &err) == -1) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_stream);
        g_object_unref (in_file);
        return NULL;
    }

    gsize enc_buf_size = (gsize) input_file_size - IV_SIZE - TAG_SIZE;
    guchar *enc_buf = g_malloc0 (enc_buf_size);

    if (!g_seekable_seek (G_SEEKABLE (in_stream), IV_SIZE, G_SEEK_SET, NULL, &err)) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_stream);
        g_object_unref (in_file);
        g_free (enc_buf);
        return NULL;
    }
    if (g_input_stream_read (G_INPUT_STREAM (in_stream), enc_buf, enc_buf_size, NULL, &err) == -1) {
        g_printerr ("%s\n", err->message);
        g_object_unref (in_stream);
        g_object_unref (in_file);
        g_free (enc_buf);
        return NULL;
    }
    g_object_unref (in_stream);
    g_object_unref (in_file);

    guchar *hashed_key = get_sha256 (password);

    gcry_cipher_open (&hd, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_GCM, 0);
    gcry_cipher_setkey (hd, hashed_key, gcry_cipher_get_algo_keylen (GCRY_CIPHER_AES256));
    gcry_cipher_setiv (hd, iv, IV_SIZE);

    gchar *decrypted_json = gcry_calloc_secure (enc_buf_size, 1);
    gcry_cipher_decrypt (hd, decrypted_json, enc_buf_size, enc_buf, enc_buf_size);
    if (gcry_err_code (gcry_cipher_checktag (hd, tag, TAG_SIZE)) == GPG_ERR_CHECKSUM) {
        g_printerr ("Invalid tag. Either the password is wrong or the file is corrupted\n");
        gcry_cipher_close (hd);
        gcry_free (hashed_key);
        g_free (enc_buf);
        return NULL;
    }

    gcry_cipher_close (hd);
    gcry_free (hashed_key);
    g_free (enc_buf);

    GSList *otps = parse_json_data (decrypted_json);
    gcry_free (decrypted_json);
    return otps;
}


guchar *
get_sha256 (const gchar *password)
{
    gcry_md_hd_t hd;
    gcry_md_open (&hd, GCRY_MD_SHA256, 0);
    gcry_md_write (hd, password, strlen (password));
    gcry_md_final (hd);

    guchar *key = gcry_calloc_secure (gcry_md_get_algo_dlen (GCRY_MD_SHA256), 1);

    guchar *tmp_hash = gcry_md_read (hd, GCRY_MD_SHA256);
    memcpy (key, tmp_hash, gcry_md_get_algo_dlen (GCRY_MD_SHA256));

    gcry_md_close (hd);

    return key;
}


GSList *
parse_json_data (const gchar *data)
{
    GError *err = NULL;

    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, data, -1, &err)) {
        g_print ("Unable to parse data: %s\n", err->message);
        g_clear_error (&err);
        g_object_unref (parser);
        return NULL; // TODO this is an error
    }

    JsonNode *root = json_parser_get_root (parser);
    g_assert (JSON_NODE_HOLDS_ARRAY (root));
    JsonArray *arr = json_node_get_array (root);

    // [{"a": 1, "b": 2}, {}, ...] this is an Array of Objects containing Nodes

    GSList *otps = NULL;
    for (guint i = 0; i < json_array_get_length (arr); i++) {
        JsonNode *node = json_array_get_element (arr, i);
        g_assert (JSON_NODE_HOLDS_OBJECT (node));
        JsonObject *object = json_node_get_object (node);

        otp_t *otp = g_new0(otp_t, 1);
        otp->secret = g_strdup (json_object_get_string_member (object, "secret"));

        const gchar *label_with_issuer = json_object_get_string_member (object, "label");
        gchar **tokens = g_strsplit (label_with_issuer, "-", -1);
        if (tokens[0] && tokens[1]) {
            otp->issuer = g_strdup (g_strstrip (tokens[0]));
            otp->label = g_strdup (g_strstrip (tokens[1]));
        } else {
            otp->label = g_strdup (g_strstrip (tokens[0]));
        }
        g_strfreev (tokens);

        otp->period = (guint8) json_object_get_int_member (object, "period");
        otp->digits = (guint8) json_object_get_int_member (object, "digits");

        const gchar *type = json_object_get_string_member (object, "type");
        if (g_ascii_strcasecmp (type, "TOTP") == 0) {
            otp->type = TYPE_TOTP;
            otp->period = 30;
        } else if (g_ascii_strcasecmp (type, "HOTP") == 0) {
            otp->type = TYPE_HOTP;
            otp->counter = 0;
        } else {
            return NULL; //TODO handle memory and exit gracefully
        }

        // TODO andOTP does not support HOTP at the moment
        const gchar *algo = json_object_get_string_member (object, "algorithm");
        if (g_ascii_strcasecmp (algo, "SHA256") == 0) {
            otp->algo = ALGO_SHA256;
        } else if (g_ascii_strcasecmp (algo, "SHA512") == 0) {
            otp->algo = ALGO_SHA512;
        } else {
            otp->algo = ALGO_SHA1;
        }

        otps = g_slist_append (otps, g_memdup (otp, sizeof (otp_t)));
        g_free (otp);
    }
    g_object_unref (parser);

    // TODO before calling slist_free_full, secret, label and issuer must be freed
    return otps;
}