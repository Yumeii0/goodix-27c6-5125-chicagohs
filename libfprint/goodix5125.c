/* Goodix 27c6:5125 native enroll/verify driver for libfprint 1.94.10. */
#define FP_COMPONENT "goodix5125"

#include "drivers_api.h"
#include "fpi-log.h"

#include "gx5125/enrollment.h"
#include "gx5125/matcher.h"
#include "gx5125/pipeline.h"
#include "gx5125/secret.h"

#include <gio/gio.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#define GOODIX5125_VID 0x27c6
#define GOODIX5125_PID 0x5125
#define GOODIX5125_ENROLL_TARGET 12U
#define GOODIX5125_ENROLL_ATTEMPTS 20U
#define GOODIX5125_VERIFY_ATTEMPTS 3U
#define GOODIX5125_THRESHOLD_Q16 48393U
#define GOODIX5125_FPPRINT_MAGIC_SIZE 8U
#define GOODIX5125_FPPRINT_VERSION 1U
#define GOODIX5125_FPPRINT_HEADER_SIZE 68U
#define GOODIX5125_FPPRINT_AAD_SIZE 52U
#define GOODIX5125_FPPRINT_SALT_SIZE 16U
#define GOODIX5125_FPPRINT_NONCE_SIZE 12U
#define GOODIX5125_FPPRINT_TAG_SIZE 16U
#define GOODIX5125_FPPRINT_FLAG_AES256_GCM_HKDF_SHA256 1U
#define GOODIX5125_FPPRINT_MAX_PLAINTEXT (1024U * 1024U)
#define GOODIX5125_AUTO_BASE_ATTEMPTS 40U
#define GOODIX5125_AUTO_FINGER_POLLS 120U
#define GOODIX5125_AUTO_LIFT_POLLS 120U
#define GOODIX5125_AUTO_POLL_DELAY_US 100000U
#define GOODIX5125_CLEAR_BASE_MIN_MEAN 2200.0
#define GOODIX5125_CLEAR_BASE_MIN_MINIMUM 1100U
#define GOODIX5125_CLEAR_BASE_MIN_MAXIMUM 2700U
#define GOODIX5125_FINGER_MIN_COVERAGE 6U

static const guint8 goodix5125_fpprint_magic[GOODIX5125_FPPRINT_MAGIC_SIZE] = {
  'G', 'X', 'F', 'P', '8', 'E', '0', '1'
};
static const guint8 goodix5125_fpprint_hkdf_info[] =
  "goodix-27c6-5125-libfprint-fpprint-v1";

typedef struct _FpiDeviceGoodix5125 FpiDeviceGoodix5125;
typedef struct _FpiDeviceGoodix5125Class FpiDeviceGoodix5125Class;

typedef struct
{
  FpPrint *template_print;
  guint8 *template_bytes;
  gsize template_size;
  guint accepted;
  guint attempts;
} GoodixEnrollOutcome;

typedef struct
{
  guint8 *template_bytes;
  gsize template_size;
} GoodixVerifyInput;

typedef struct
{
  guint score_q16;
  guint matched_pairs;
  gboolean matched;
  guint attempts;
} GoodixVerifyOutcome;

typedef struct
{
  FpDevice *device;
  gint completed;
  GError *error;
  GMutex mutex;
  GCond cond;
  gboolean done;
} GoodixProgressEvent;

struct _FpiDeviceGoodix5125Class
{
  FpDeviceClass parent_class;
};

struct _FpiDeviceGoodix5125
{
  FpDevice parent;
  GMutex mutex;
  gx5125_pipeline *pipeline;
};

GType fpi_device_goodix5125_get_type (void);
G_DEFINE_TYPE (FpiDeviceGoodix5125, fpi_device_goodix5125, FP_TYPE_DEVICE)

static void
secure_clear (gpointer data,
              gsize    size)
{
  volatile guint8 *bytes = data;
  while (bytes != NULL && size-- > 0)
    *bytes++ = 0;
}

static void
store_le32 (guint8  bytes[4],
            uint32_t value)
{
  bytes[0] = (guint8) (value & 0xffU);
  bytes[1] = (guint8) ((value >> 8U) & 0xffU);
  bytes[2] = (guint8) ((value >> 16U) & 0xffU);
  bytes[3] = (guint8) ((value >> 24U) & 0xffU);
}

static uint32_t
load_le32 (const guint8 bytes[4])
{
  return (uint32_t) bytes[0] |
         ((uint32_t) bytes[1] << 8U) |
         ((uint32_t) bytes[2] << 16U) |
         ((uint32_t) bytes[3] << 24U);
}

static gboolean
derive_fpprint_key (const guint8 psk[GX5125_PSK_SIZE],
                    const guint8 salt[GOODIX5125_FPPRINT_SALT_SIZE],
                    guint8       key[32])
{
  EVP_PKEY_CTX *context = NULL;
  size_t key_size = 32U;
  gboolean ok = FALSE;

  context = EVP_PKEY_CTX_new_id (EVP_PKEY_HKDF, NULL);
  if (context != NULL &&
      EVP_PKEY_derive_init (context) > 0 &&
      EVP_PKEY_CTX_set_hkdf_md (context, EVP_sha256 ()) > 0 &&
      EVP_PKEY_CTX_set1_hkdf_salt (context, salt,
                                   GOODIX5125_FPPRINT_SALT_SIZE) > 0 &&
      EVP_PKEY_CTX_set1_hkdf_key (context, psk, GX5125_PSK_SIZE) > 0 &&
      EVP_PKEY_CTX_add1_hkdf_info (context,
                                   goodix5125_fpprint_hkdf_info,
                                   sizeof goodix5125_fpprint_hkdf_info - 1U) > 0 &&
      EVP_PKEY_derive (context, key, &key_size) > 0 &&
      key_size == 32U)
    ok = TRUE;

  EVP_PKEY_CTX_free (context);
  if (!ok)
    secure_clear (key, 32U);
  return ok;
}

static gboolean
read_fpprint_psk (guint8  psk[GX5125_PSK_SIZE],
                  GError **error)
{
  const gchar *path = g_getenv ("GOODIX5125_PSK_PATH");

  if (path == NULL || path[0] == '\0')
    path = "/etc/goodix-27c6-5125/psk.hex";
  if (gx_secret_read_psk_file (path, psk) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "cannot read secure Goodix PSK file");
      return FALSE;
    }
  return TRUE;
}

static gboolean
encrypt_fpprint_payload (const guint8 *plaintext,
                         gsize         plaintext_size,
                         guint8      **encrypted,
                         gsize        *encrypted_size,
                         GError      **error)
{
  guint8 header[GOODIX5125_FPPRINT_HEADER_SIZE] = { 0 };
  guint8 psk[GX5125_PSK_SIZE] = { 0 };
  guint8 key[32] = { 0 };
  guint8 *output = NULL;
  EVP_CIPHER_CTX *context = NULL;
  int output_length = 0;
  int final_length = 0;
  gboolean ok = FALSE;

  g_return_val_if_fail (encrypted != NULL, FALSE);
  g_return_val_if_fail (encrypted_size != NULL, FALSE);
  *encrypted = NULL;
  *encrypted_size = 0;

  if (plaintext == NULL || plaintext_size == 0 ||
      plaintext_size > GOODIX5125_FPPRINT_MAX_PLAINTEXT ||
      plaintext_size > (gsize) INT_MAX)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Goodix native template size is invalid");
      goto out;
    }
  if (!read_fpprint_psk (psk, error))
    goto out;

  output = g_malloc (GOODIX5125_FPPRINT_HEADER_SIZE + plaintext_size);
  memcpy (header, goodix5125_fpprint_magic, GOODIX5125_FPPRINT_MAGIC_SIZE);
  store_le32 (header + 8U, GOODIX5125_FPPRINT_VERSION);
  store_le32 (header + 12U, GOODIX5125_FPPRINT_HEADER_SIZE);
  store_le32 (header + 16U, (uint32_t) plaintext_size);
  store_le32 (header + 20U,
              GOODIX5125_FPPRINT_FLAG_AES256_GCM_HKDF_SHA256);

  if (RAND_bytes (header + 24U, GOODIX5125_FPPRINT_SALT_SIZE) != 1 ||
      RAND_bytes (header + 40U, GOODIX5125_FPPRINT_NONCE_SIZE) != 1 ||
      !derive_fpprint_key (psk, header + 24U, key))
    goto crypto_error;

  context = EVP_CIPHER_CTX_new ();
  if (context == NULL ||
      EVP_EncryptInit_ex (context, EVP_aes_256_gcm (), NULL, NULL, NULL) != 1 ||
      EVP_CIPHER_CTX_ctrl (context, EVP_CTRL_GCM_SET_IVLEN,
                           GOODIX5125_FPPRINT_NONCE_SIZE, NULL) != 1 ||
      EVP_EncryptInit_ex (context, NULL, NULL, key, header + 40U) != 1 ||
      EVP_EncryptUpdate (context, NULL, &output_length,
                         header, GOODIX5125_FPPRINT_AAD_SIZE) != 1 ||
      EVP_EncryptUpdate (context,
                         output + GOODIX5125_FPPRINT_HEADER_SIZE,
                         &output_length, plaintext,
                         (int) plaintext_size) != 1 ||
      output_length != (int) plaintext_size ||
      EVP_EncryptFinal_ex (context,
                           output + GOODIX5125_FPPRINT_HEADER_SIZE + output_length,
                           &final_length) != 1 ||
      final_length != 0 ||
      EVP_CIPHER_CTX_ctrl (context, EVP_CTRL_GCM_GET_TAG,
                           GOODIX5125_FPPRINT_TAG_SIZE, header + 52U) != 1)
    goto crypto_error;

  memcpy (output, header, sizeof header);
  *encrypted = output;
  *encrypted_size = GOODIX5125_FPPRINT_HEADER_SIZE + plaintext_size;
  output = NULL;
  ok = TRUE;
  goto out;

crypto_error:
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               "Goodix FpPrint payload encryption failed");
out:
  EVP_CIPHER_CTX_free (context);
  if (output != NULL)
    {
      secure_clear (output, GOODIX5125_FPPRINT_HEADER_SIZE + plaintext_size);
      g_free (output);
    }
  secure_clear (header, sizeof header);
  secure_clear (psk, sizeof psk);
  secure_clear (key, sizeof key);
  return ok;
}

static gboolean
decrypt_fpprint_payload (const guint8 *encrypted,
                         gsize         encrypted_size,
                         guint8      **plaintext,
                         gsize        *plaintext_size,
                         GError      **error)
{
  guint8 psk[GX5125_PSK_SIZE] = { 0 };
  guint8 key[32] = { 0 };
  guint8 *output = NULL;
  EVP_CIPHER_CTX *context = NULL;
  uint32_t encoded_size = 0U;
  int output_length = 0;
  int final_length = 0;
  gboolean ok = FALSE;

  g_return_val_if_fail (plaintext != NULL, FALSE);
  g_return_val_if_fail (plaintext_size != NULL, FALSE);
  *plaintext = NULL;
  *plaintext_size = 0;

  if (encrypted == NULL ||
      encrypted_size < GOODIX5125_FPPRINT_HEADER_SIZE ||
      encrypted_size > GOODIX5125_FPPRINT_HEADER_SIZE +
                       GOODIX5125_FPPRINT_MAX_PLAINTEXT ||
      memcmp (encrypted, goodix5125_fpprint_magic,
              GOODIX5125_FPPRINT_MAGIC_SIZE) != 0 ||
      load_le32 (encrypted + 8U) != GOODIX5125_FPPRINT_VERSION ||
      load_le32 (encrypted + 12U) != GOODIX5125_FPPRINT_HEADER_SIZE ||
      load_le32 (encrypted + 20U) !=
        GOODIX5125_FPPRINT_FLAG_AES256_GCM_HKDF_SHA256)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Goodix FpPrint encrypted payload header is invalid");
      goto out;
    }

  encoded_size = load_le32 (encrypted + 16U);
  if (encoded_size == 0U ||
      encoded_size > GOODIX5125_FPPRINT_MAX_PLAINTEXT ||
      encrypted_size != GOODIX5125_FPPRINT_HEADER_SIZE + (gsize) encoded_size ||
      encoded_size > (uint32_t) INT_MAX)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Goodix FpPrint encrypted payload size is invalid");
      goto out;
    }
  if (!read_fpprint_psk (psk, error))
    goto out;
  if (!derive_fpprint_key (psk, encrypted + 24U, key))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Goodix FpPrint key derivation failed");
      goto out;
    }

  output = g_malloc (encoded_size);
  context = EVP_CIPHER_CTX_new ();
  if (context == NULL ||
      EVP_DecryptInit_ex (context, EVP_aes_256_gcm (), NULL, NULL, NULL) != 1 ||
      EVP_CIPHER_CTX_ctrl (context, EVP_CTRL_GCM_SET_IVLEN,
                           GOODIX5125_FPPRINT_NONCE_SIZE, NULL) != 1 ||
      EVP_DecryptInit_ex (context, NULL, NULL, key, encrypted + 40U) != 1 ||
      EVP_DecryptUpdate (context, NULL, &output_length,
                         encrypted, GOODIX5125_FPPRINT_AAD_SIZE) != 1 ||
      EVP_DecryptUpdate (context, output, &output_length,
                         encrypted + GOODIX5125_FPPRINT_HEADER_SIZE,
                         (int) encoded_size) != 1 ||
      output_length != (int) encoded_size ||
      EVP_CIPHER_CTX_ctrl (context, EVP_CTRL_GCM_SET_TAG,
                           GOODIX5125_FPPRINT_TAG_SIZE,
                           (gpointer) (encrypted + 52U)) != 1 ||
      EVP_DecryptFinal_ex (context, output + output_length,
                           &final_length) != 1 ||
      final_length != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Goodix FpPrint payload authentication failed");
      goto out;
    }

  *plaintext = output;
  *plaintext_size = encoded_size;
  output = NULL;
  ok = TRUE;
out:
  EVP_CIPHER_CTX_free (context);
  if (output != NULL)
    {
      secure_clear (output, encoded_size);
      g_free (output);
    }
  secure_clear (psk, sizeof psk);
  secure_clear (key, sizeof key);
  return ok;
}

static GError *
goodix_error (const gchar *operation,
              int          status)
{
  const gchar *text = gx5125_pipeline_status_string (status);

  if (status == GX5125_PIPELINE_ERR_CANCELLED)
    return g_error_new (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                        "%s cancelled", operation);

  return fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                   "%s failed: %s (%d)", operation,
                                   text != NULL ? text : "unknown", status);
}

static gboolean
capture_clear_base (FpDevice         *device,
                    gx5125_pipeline *pipeline,
                    const gchar     *operation,
                    GError         **error)
{
  guint attempt;

  for (attempt = 1; attempt <= GOODIX5125_AUTO_BASE_ATTEMPTS; attempt++)
    {
      gx5125_capture_metadata metadata = { 0 };
      int status;

      if (fpi_device_action_is_cancelled (device))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                       "%s cancelled", operation);
          return FALSE;
        }

      status = gx5125_pipeline_capture_base (pipeline, &metadata);
      if (status == GX5125_PIPELINE_ERR_CANCELLED)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                       "%s cancelled", operation);
          return FALSE;
        }
      if (status == GX5125_PIPELINE_OK &&
          metadata.mean >= GOODIX5125_CLEAR_BASE_MIN_MEAN &&
          metadata.minimum >= GOODIX5125_CLEAR_BASE_MIN_MINIMUM &&
          metadata.maximum >= GOODIX5125_CLEAR_BASE_MIN_MAXIMUM)
        {
          g_print ("GOODIX5125_AUTO_BASE=PASS operation:%s attempts:%u minimum:%u mean:%.3f maximum:%u sensor_empty:1 interactive_prompt:0\n",
                   operation, attempt, metadata.minimum, metadata.mean, metadata.maximum);
          return TRUE;
        }

      g_usleep (GOODIX5125_AUTO_POLL_DELAY_US);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
               "%s could not capture an empty-sensor base frame", operation);
  return FALSE;
}

static int
capture_feature_when_present (FpDevice               *device,
                              gx5125_pipeline       *pipeline,
                              gx5125_extract_mode    mode,
                              gx5125_feature       **feature,
                              gx5125_pipeline_result *result,
                              guint                 *polls)
{
  guint poll;

  g_return_val_if_fail (feature != NULL, GX5125_PIPELINE_ERR_ARGUMENT);
  g_return_val_if_fail (result != NULL, GX5125_PIPELINE_ERR_ARGUMENT);
  *feature = NULL;
  memset (result, 0, sizeof *result);

  for (poll = 1; poll <= GOODIX5125_AUTO_FINGER_POLLS; poll++)
    {
      int status;

      if (fpi_device_action_is_cancelled (device))
        return GX5125_PIPELINE_ERR_CANCELLED;

      memset (result, 0, sizeof *result);
      status = gx5125_pipeline_capture_feature (pipeline, mode, NULL,
                                                feature, result);
      if (status == GX5125_PIPELINE_OK && *feature != NULL)
        {
          if (polls != NULL)
            *polls = poll;
          return status;
        }
      gx5125_feature_destroy (*feature);
      *feature = NULL;

      if (status == GX5125_PIPELINE_ERR_CANCELLED)
        return status;

      if (result->preprocess.coverage >= GOODIX5125_FINGER_MIN_COVERAGE)
        {
          if (polls != NULL)
            *polls = poll;
          return status;
        }

      g_usleep (GOODIX5125_AUTO_POLL_DELAY_US);
    }

  if (polls != NULL)
    *polls = GOODIX5125_AUTO_FINGER_POLLS;
  return GX5125_PIPELINE_ERR_FINGER_CAPTURE;
}

static gboolean
wait_for_finger_absent (FpDevice         *device,
                        gx5125_pipeline *pipeline,
                        const gchar     *operation,
                        GError         **error)
{
  guint poll;
  guint consecutive_absent = 0;

  for (poll = 1; poll <= GOODIX5125_AUTO_LIFT_POLLS; poll++)
    {
      gx5125_presence_result presence = { 0 };
      int status;

      if (fpi_device_action_is_cancelled (device))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                       "%s cancelled", operation);
          return FALSE;
        }

      status = gx5125_pipeline_capture_presence (pipeline, &presence);
      if (status == GX5125_PIPELINE_ERR_CANCELLED)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                       "%s cancelled", operation);
          return FALSE;
        }
      if (status == GX5125_PIPELINE_OK &&
          presence.state == GX5125_FINGER_ABSENT)
        consecutive_absent++;
      else
        consecutive_absent = 0;

      if (consecutive_absent >= 2U)
        {
          g_print ("GOODIX5125_AUTO_LIFT=PASS operation:%s polls:%u sensor_empty:1 interactive_prompt:0\n",
                   operation, poll);
          return TRUE;
        }
      g_usleep (GOODIX5125_AUTO_POLL_DELAY_US);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
               "%s timed out waiting for the finger to be removed", operation);
  return FALSE;
}

static gboolean
emit_progress_main (gpointer user_data)
{
  GoodixProgressEvent *event = user_data;

  fpi_device_enroll_progress (event->device,
                              event->completed,
                              NULL,
                              event->error);
  g_mutex_lock (&event->mutex);
  event->done = TRUE;
  g_cond_signal (&event->cond);
  g_mutex_unlock (&event->mutex);
  return G_SOURCE_REMOVE;
}

static void
queue_progress (FpDevice *device,
                gint      completed,
                GError   *error)
{
  GoodixProgressEvent *event = g_new0 (GoodixProgressEvent, 1);
  GSource *source;

  event->device = g_object_ref (device);
  event->completed = completed;
  event->error = error;
  g_mutex_init (&event->mutex);
  g_cond_init (&event->cond);

  source = g_idle_source_new ();
  g_source_set_callback (source, emit_progress_main, event, NULL);
  g_source_attach (source, g_main_context_default ());
  g_source_unref (source);

  g_mutex_lock (&event->mutex);
  while (!event->done)
    g_cond_wait (&event->cond, &event->mutex);
  g_mutex_unlock (&event->mutex);

  g_cond_clear (&event->cond);
  g_mutex_clear (&event->mutex);
  g_object_unref (event->device);
  g_free (event);
}

static void
enroll_outcome_free (GoodixEnrollOutcome *outcome)
{
  if (outcome == NULL)
    return;
  if (outcome->template_bytes != NULL)
    {
      secure_clear (outcome->template_bytes, outcome->template_size);
      g_free (outcome->template_bytes);
    }
  g_clear_object (&outcome->template_print);
  g_free (outcome);
}

static void
verify_input_free (GoodixVerifyInput *input)
{
  if (input == NULL)
    return;
  if (input->template_bytes != NULL)
    {
      secure_clear (input->template_bytes, input->template_size);
      g_free (input->template_bytes);
    }
  g_free (input);
}

static void
open_worker (GTask        *task,
             gpointer      source_object,
             gpointer      task_data,
             GCancellable *cancellable)
{
  FpiDeviceGoodix5125 *self = source_object;
  gx5125_pipeline_config config;
  gx5125_pipeline *pipeline;
  const gchar *psk_path;
  int status;

  (void) task_data;
  (void) cancellable;

  gx5125_pipeline_default_config (&config);
  psk_path = g_getenv ("GOODIX5125_PSK_PATH");
  if (psk_path != NULL && psk_path[0] != '\0')
    config.device.psk_path = psk_path;

  pipeline = gx5125_pipeline_create (&config);
  if (pipeline == NULL)
    {
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                     "native pipeline allocation failed"));
      return;
    }

  g_mutex_lock (&self->mutex);
  if (self->pipeline != NULL)
    {
      g_mutex_unlock (&self->mutex);
      gx5125_pipeline_destroy (pipeline);
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_BUSY,
                                                     "native pipeline is already open"));
      return;
    }
  self->pipeline = pipeline;
  g_mutex_unlock (&self->mutex);

  status = gx5125_pipeline_open (pipeline);
  if (status != GX5125_PIPELINE_OK)
    {
      g_mutex_lock (&self->mutex);
      if (self->pipeline == pipeline)
        self->pipeline = NULL;
      g_mutex_unlock (&self->mutex);
      gx5125_pipeline_close (pipeline);
      gx5125_pipeline_destroy (pipeline);
      g_task_return_error (task, goodix_error ("native open", status));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
open_done (GObject      *source_object,
           GAsyncResult *result,
           gpointer      user_data)
{
  GError *error = NULL;
  (void) user_data;
  if (g_task_propagate_boolean (G_TASK (result), &error))
    g_print ("GOODIX5125_DRIVER_OPEN=PASS native_usb_tls:1 automatic_touch_state:1\n");
  fpi_device_open_complete (FP_DEVICE (source_object), error);
}

static void
goodix_open (FpDevice *device)
{
  GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                            open_done, NULL);
  g_task_run_in_thread (task, open_worker);
  g_object_unref (task);
}

static void
close_worker (GTask        *task,
              gpointer      source_object,
              gpointer      task_data,
              GCancellable *cancellable)
{
  FpiDeviceGoodix5125 *self = source_object;
  gx5125_pipeline *pipeline;
  (void) task_data;
  (void) cancellable;

  g_mutex_lock (&self->mutex);
  pipeline = self->pipeline;
  self->pipeline = NULL;
  g_mutex_unlock (&self->mutex);

  if (pipeline != NULL)
    {
      gx5125_pipeline_close (pipeline);
      gx5125_pipeline_destroy (pipeline);
    }
  g_task_return_boolean (task, TRUE);
}

static void
close_done (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
  GError *error = NULL;
  (void) user_data;
  if (g_task_propagate_boolean (G_TASK (result), &error))
    g_print ("GOODIX5125_DRIVER_CLOSE=PASS native_usb_tls:1\n");
  fpi_device_close_complete (FP_DEVICE (source_object), error);
}

static void
goodix_close (FpDevice *device)
{
  GTask *task = g_task_new (device, fpi_device_get_cancellable (device),
                            close_done, NULL);
  g_task_run_in_thread (task, close_worker);
  g_object_unref (task);
}

static gx5125_pipeline *
get_pipeline (FpiDeviceGoodix5125 *self)
{
  gx5125_pipeline *pipeline;
  g_mutex_lock (&self->mutex);
  pipeline = self->pipeline;
  g_mutex_unlock (&self->mutex);
  return pipeline;
}

static void
enroll_worker (GTask        *task,
               gpointer      source_object,
               gpointer      task_data,
               GCancellable *cancellable)
{
  FpiDeviceGoodix5125 *self = source_object;
  FpDevice *device = FP_DEVICE (source_object);
  FpPrint *template_print = task_data;
  gx5125_pipeline *pipeline = get_pipeline (self);
  gx5125_enrollment_config enroll_config;
  gx5125_enrollment *enrollment = NULL;
  GoodixEnrollOutcome *outcome = NULL;
  GError *error = NULL;
  guint accepted = 0;
  guint attempt;
  int status;

  (void) cancellable;

  if (pipeline == NULL)
    {
      g_task_return_error (task, fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  gx5125_pipeline_clear_cancel (pipeline);
  gx5125_pipeline_reset_processing (pipeline);
  if (!capture_clear_base (device, pipeline, "enroll", &error))
    {
      g_task_return_error (task, error);
      return;
    }

  gx5125_enrollment_default_config (&enroll_config);
  enroll_config.target_samples = GOODIX5125_ENROLL_TARGET;
  enrollment = gx5125_enrollment_create (&enroll_config);
  if (enrollment == NULL)
    {
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                     "enrollment allocation failed"));
      return;
    }

  for (attempt = 1; attempt <= GOODIX5125_ENROLL_ATTEMPTS && accepted < GOODIX5125_ENROLL_TARGET; attempt++)
    {
      gx5125_feature *feature = NULL;
      gx5125_pipeline_result pipeline_result = { 0 };
      gx5125_enrollment_metrics metrics = { 0 };
      gx5125_enrollment_result enroll_result = { 0 };
      guint finger_polls = 0;

      if (fpi_device_action_is_cancelled (device))
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "enrollment cancelled");
          return;
        }

      status = capture_feature_when_present (device, pipeline,
                                             GX5125_EXTRACT_ENROLL,
                                             &feature, &pipeline_result,
                                             &finger_polls);
      if (status == GX5125_PIPELINE_ERR_CANCELLED)
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "enrollment cancelled");
          return;
        }
      if (status != GX5125_PIPELINE_OK || feature == NULL)
        {
          gx5125_feature_destroy (feature);
          queue_progress (device, (gint) accepted,
                          fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL,
                                                    "finger image could not be used"));
          if (!wait_for_finger_absent (device, pipeline,
                                       "enroll-retry-lift", &error))
            {
              gx5125_enrollment_destroy (enrollment);
              g_task_return_error (task, error);
              return;
            }
          continue;
        }
      g_print ("GOODIX5125_AUTO_TOUCH=PASS operation:enroll attempt:%u polls:%u coverage:%u quality:%u interactive_prompt:0\n",
               attempt, finger_polls, pipeline_result.preprocess.coverage,
               pipeline_result.preprocess.quality);

      metrics.quality = pipeline_result.preprocess.quality;
      metrics.coverage = pipeline_result.preprocess.coverage;
      metrics.mask_coverage_q16 = pipeline_result.extract.mask_coverage_q16;
      status = gx5125_enrollment_submit (enrollment, feature, &metrics,
                                         &enroll_result);
      gx5125_feature_destroy (feature);
      if (status != GX5125_ENROLLMENT_OK)
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_error (task,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                         "enrollment submit failed: %s (%d)",
                                                         gx5125_enrollment_status_string (status),
                                                         status));
          return;
        }

      if (enroll_result.decision != GX5125_ENROLLMENT_ACCEPTED)
        {
          queue_progress (device, (gint) accepted,
                          fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL,
                                                    "sample rejected: %s",
                                                    gx5125_enrollment_decision_string (enroll_result.decision)));
          if (!wait_for_finger_absent (device, pipeline,
                                       "enroll-rejected-lift", &error))
            {
              gx5125_enrollment_destroy (enrollment);
              g_task_return_error (task, error);
              return;
            }
          continue;
        }

      accepted = enroll_result.accepted_samples;
      queue_progress (device, (gint) accepted, NULL);
      if (accepted < GOODIX5125_ENROLL_TARGET &&
          !wait_for_finger_absent (device, pipeline,
                                   "enroll-next-stage-lift", &error))
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_error (task, error);
          return;
        }
    }

  if (accepted != GOODIX5125_ENROLL_TARGET || !gx5125_enrollment_is_complete (enrollment))
    {
      gx5125_enrollment_destroy (enrollment);
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                     "enrollment incomplete: %u/%u",
                                                     accepted, GOODIX5125_ENROLL_TARGET));
      return;
    }

  outcome = g_new0 (GoodixEnrollOutcome, 1);
  outcome->template_print = g_object_ref (template_print);
  outcome->template_size = gx5125_enrollment_serialized_size (enrollment);
  if (outcome->template_size == 0)
    {
      gx5125_enrollment_destroy (enrollment);
      enroll_outcome_free (outcome);
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                     "template serialization size is zero"));
      return;
    }
  outcome->template_bytes = g_malloc (outcome->template_size);
  {
    const gsize capacity = outcome->template_size;
    size_t written = 0;

    status = gx5125_enrollment_serialize (enrollment,
                                          outcome->template_bytes,
                                          capacity,
                                          &written);
    gx5125_enrollment_destroy (enrollment);
    if (status == GX5125_ENROLLMENT_OK && written != capacity)
      status = GX5125_ENROLLMENT_ERR_FORMAT;
    if (status == GX5125_ENROLLMENT_OK)
      outcome->template_size = written;
  }
  if (status != GX5125_ENROLLMENT_OK)
    {
      enroll_outcome_free (outcome);
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                     "template serialization failed: %s (%d)",
                                                     gx5125_enrollment_status_string (status),
                                                     status));
      return;
    }
  outcome->accepted = accepted;
  outcome->attempts = attempt - 1;
  g_task_return_pointer (task, outcome, (GDestroyNotify) enroll_outcome_free);
}

static void
enroll_done (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
  FpDevice *device = FP_DEVICE (source_object);
  GError *error = NULL;
  GoodixEnrollOutcome *outcome;
  g_autoptr(GVariant) data = NULL;
  guint8 *encrypted = NULL;
  gsize encrypted_size = 0;
  (void) user_data;

  outcome = g_task_propagate_pointer (G_TASK (result), &error);
  if (outcome == NULL)
    {
      fpi_device_enroll_complete (device, NULL, error);
      return;
    }

  if (!encrypt_fpprint_payload (outcome->template_bytes,
                                outcome->template_size,
                                &encrypted,
                                &encrypted_size,
                                &error))
    {
      enroll_outcome_free (outcome);
      fpi_device_enroll_complete (device, NULL, error);
      return;
    }

  fpi_print_set_type (outcome->template_print, FPI_PRINT_RAW);
  data = g_variant_ref_sink (g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                        encrypted,
                                                        encrypted_size,
                                                        sizeof (guint8)));
  g_object_set (outcome->template_print, "fpi-data", data, NULL);
  g_print ("GOODIX5125_DRIVER_ENROLL=PASS stages:%u attempts:%u native_template_bytes:%zu encrypted_fpprint_bytes:%zu fpprint_raw:1 aes256gcm:1 hkdf_sha256:1 template_plaintext_saved:0\n",
           outcome->accepted, outcome->attempts, outcome->template_size,
           encrypted_size);
  fpi_device_enroll_complete (device,
                              g_object_ref (outcome->template_print),
                              NULL);
  secure_clear (encrypted, encrypted_size);
  g_free (encrypted);
  enroll_outcome_free (outcome);
}

static void
goodix_enroll (FpDevice *device)
{
  FpPrint *template_print = NULL;
  GTask *task;

  fpi_device_get_enroll_data (device, &template_print);
  task = g_task_new (device, fpi_device_get_cancellable (device),
                     enroll_done, NULL);
  g_task_set_task_data (task, g_object_ref (template_print), g_object_unref);
  g_task_run_in_thread (task, enroll_worker);
  g_object_unref (task);
}

static void
verify_worker (GTask        *task,
               gpointer      source_object,
               gpointer      task_data,
               GCancellable *cancellable)
{
  FpiDeviceGoodix5125 *self = source_object;
  FpDevice *device = FP_DEVICE (source_object);
  GoodixVerifyInput *input = task_data;
  gx5125_pipeline *pipeline = get_pipeline (self);
  gx5125_enrollment *enrollment = NULL;
  gx5125_matcher_config matcher_config;
  GoodixVerifyOutcome *outcome = NULL;
  GError *error = NULL;
  guint attempt;
  int status;

  (void) cancellable;
  if (pipeline == NULL)
    {
      g_task_return_error (task, fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  status = gx5125_enrollment_deserialize (input->template_bytes,
                                          input->template_size,
                                          &enrollment);
  if (status != GX5125_ENROLLMENT_OK || enrollment == NULL)
    {
      g_task_return_error (task,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID,
                                                     "FpPrint template invalid: %s (%d)",
                                                     gx5125_enrollment_status_string (status),
                                                     status));
      return;
    }

  gx5125_pipeline_clear_cancel (pipeline);
  gx5125_pipeline_reset_processing (pipeline);
  if (!capture_clear_base (device, pipeline, "verify", &error))
    {
      gx5125_enrollment_destroy (enrollment);
      g_task_return_error (task, error);
      return;
    }

  gx5125_matcher_default_config (&matcher_config);
  matcher_config.decision_threshold_q16 = GOODIX5125_THRESHOLD_Q16;

  for (attempt = 1; attempt <= GOODIX5125_VERIFY_ATTEMPTS; attempt++)
    {
      gx5125_feature *feature = NULL;
      gx5125_pipeline_result pipeline_result = { 0 };
      gx5125_match_result match_result = { 0 };
      guint finger_polls = 0;

      if (fpi_device_action_is_cancelled (device))
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "verification cancelled");
          return;
        }

      status = capture_feature_when_present (device, pipeline,
                                             GX5125_EXTRACT_IDENTIFY,
                                             &feature, &pipeline_result,
                                             &finger_polls);
      if (status == GX5125_PIPELINE_ERR_CANCELLED)
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "verification cancelled");
          return;
        }
      if (status != GX5125_PIPELINE_OK || feature == NULL)
        {
          gx5125_feature_destroy (feature);
          if (!wait_for_finger_absent (device, pipeline,
                                       "verify-retry-lift", &error))
            {
              gx5125_enrollment_destroy (enrollment);
              g_task_return_error (task, error);
              return;
            }
          continue;
        }
      g_print ("GOODIX5125_AUTO_TOUCH=PASS operation:verify attempt:%u polls:%u coverage:%u quality:%u interactive_prompt:0\n",
               attempt, finger_polls, pipeline_result.preprocess.coverage,
               pipeline_result.preprocess.quality);
      status = gx5125_matcher_score_enrollment (enrollment, feature,
                                                &matcher_config,
                                                &match_result);
      gx5125_feature_destroy (feature);
      if (status != GX5125_MATCHER_OK)
        {
          gx5125_enrollment_destroy (enrollment);
          g_task_return_error (task,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                         "matcher failed: %s (%d)",
                                                         gx5125_matcher_status_string (status),
                                                         status));
          return;
        }

      outcome = g_new0 (GoodixVerifyOutcome, 1);
      outcome->score_q16 = match_result.score_q16;
      outcome->matched_pairs = match_result.matched_pairs;
      outcome->matched = match_result.matched != 0;
      outcome->attempts = attempt;
      gx5125_enrollment_destroy (enrollment);
      g_task_return_pointer (task, outcome, g_free);
      return;
    }

  gx5125_enrollment_destroy (enrollment);
  g_task_return_error (task,
                       fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL,
                                                 "no usable verification capture"));
}

static void
verify_done (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
  FpDevice *device = FP_DEVICE (source_object);
  GError *error = NULL;
  GoodixVerifyOutcome *outcome;
  (void) user_data;

  outcome = g_task_propagate_pointer (G_TASK (result), &error);
  if (outcome == NULL)
    {
      if (error != NULL && error->domain == FP_DEVICE_RETRY)
        {
          fpi_device_verify_report (device, FPI_MATCH_ERROR, NULL, error);
          fpi_device_verify_complete (device, NULL);
        }
      else
        fpi_device_verify_complete (device, error);
      return;
    }

  g_print ("GOODIX5125_DRIVER_VERIFY=PASS score_q16:%u threshold_q16:%u matched_pairs:%u matched:%u attempts:%u\n",
           outcome->score_q16, GOODIX5125_THRESHOLD_Q16,
           outcome->matched_pairs, outcome->matched, outcome->attempts);
  fpi_device_verify_report (device,
                            outcome->matched ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                            NULL,
                            NULL);
  fpi_device_verify_complete (device, NULL);
  g_free (outcome);
}

static void
goodix_verify (FpDevice *device)
{
  FpPrint *print = NULL;
  GVariant *data = NULL;
  const guint8 *bytes;
  gsize size = 0;
  GoodixVerifyInput *input;
  GTask *task;
  GError *error = NULL;
  guint8 *plaintext = NULL;
  gsize plaintext_size = 0;

  fpi_device_get_verify_data (device, &print);
  g_object_get (print, "fpi-data", &data, NULL);
  if (data == NULL || !g_variant_is_of_type (data, G_VARIANT_TYPE ("ay")))
    {
      g_clear_pointer (&data, g_variant_unref);
      fpi_device_verify_complete (device,
                                  fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID,
                                                            "FpPrint payload is not a byte array"));
      return;
    }

  bytes = g_variant_get_fixed_array (data, &size, sizeof (guint8));
  if (bytes == NULL || size == 0 ||
      !decrypt_fpprint_payload (bytes, size,
                                &plaintext, &plaintext_size, &error))
    {
      g_variant_unref (data);
      if (error == NULL)
        error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID,
                                          "FpPrint encrypted payload is empty");
      fpi_device_verify_complete (device, error);
      return;
    }
  g_variant_unref (data);

  g_print ("GOODIX5125_FPPRINT_DECRYPT=PASS encrypted_bytes:%zu native_template_bytes:%zu aes256gcm_authenticated:1 hkdf_sha256:1 plaintext_saved:0\n",
           size, plaintext_size);
  input = g_new0 (GoodixVerifyInput, 1);
  input->template_bytes = plaintext;
  input->template_size = plaintext_size;

  task = g_task_new (device, fpi_device_get_cancellable (device),
                     verify_done, NULL);
  g_task_set_task_data (task, input, (GDestroyNotify) verify_input_free);
  g_task_run_in_thread (task, verify_worker);
  g_object_unref (task);
}

static void
goodix_cancel (FpDevice *device)
{
  FpiDeviceGoodix5125 *self = (FpiDeviceGoodix5125 *) device;
  g_mutex_lock (&self->mutex);
  if (self->pipeline != NULL)
    gx5125_pipeline_request_cancel (self->pipeline);
  g_mutex_unlock (&self->mutex);
}

static void
goodix_finalize (GObject *object)
{
  FpiDeviceGoodix5125 *self = (FpiDeviceGoodix5125 *) object;
  gx5125_pipeline *pipeline;

  g_mutex_lock (&self->mutex);
  pipeline = self->pipeline;
  self->pipeline = NULL;
  g_mutex_unlock (&self->mutex);
  if (pipeline != NULL)
    {
      gx5125_pipeline_request_cancel (pipeline);
      gx5125_pipeline_close (pipeline);
      gx5125_pipeline_destroy (pipeline);
    }
  g_mutex_clear (&self->mutex);
  G_OBJECT_CLASS (fpi_device_goodix5125_parent_class)->finalize (object);
}

static void
fpi_device_goodix5125_init (FpiDeviceGoodix5125 *self)
{
  g_mutex_init (&self->mutex);
  self->pipeline = NULL;
}

static const FpIdEntry driver_ids[] = {
  { .vid = GOODIX5125_VID, .pid = GOODIX5125_PID },
  { .vid = 0, .pid = 0 }
};

static void
fpi_device_goodix5125_class_init (FpiDeviceGoodix5125Class *klass)
{
  FpDeviceClass *device_class = FP_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = goodix_finalize;
  device_class->id = FP_COMPONENT;
  device_class->full_name = "Goodix 27c6:5125 ChicagoHS native";
  device_class->type = FP_DEVICE_TYPE_USB;
  device_class->id_table = driver_ids;
  device_class->nr_enroll_stages = GOODIX5125_ENROLL_TARGET;
  device_class->scan_type = FP_SCAN_TYPE_PRESS;
  device_class->temp_hot_seconds = -1;
  device_class->temp_cold_seconds = 0;
  device_class->open = goodix_open;
  device_class->close = goodix_close;
  device_class->enroll = goodix_enroll;
  device_class->verify = goodix_verify;
  device_class->cancel = goodix_cancel;
  fpi_device_class_auto_initialize_features (device_class);
}
