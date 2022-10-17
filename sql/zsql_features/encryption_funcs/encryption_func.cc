#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <climits>
#include <openssl/err.h>
#include <openssl/bio.h>
#include "sql/current_thd.h"
#include "sql/sql_error.h"
#include "sql/derror.h"
#include "sql/zsql_features/encryption_funcs/encryption_func.h"

using namespace std;



#define MIN_RSA_KEY_LEN 2048
#define MAX_RSA_KEY_LEN 16384

#define PKCS1_PADDING_LEN 11

#define MAX_BUFFER_LEN 2048

// RSA key format
#define PRIV_KEY_HEADER "PRIVATE KEY"
#define PRIV_KEY_HEADER_LEN my_strlen(PRIV_KEY_HEADER)
#define PUB_KEY_HEADER "PUBLIC KEY"
#define PUB_KEY_HEADER_LEN my_strlen(PUB_KEY_HEADER)

static inline void push_err_msg(bool error, const std::string &msg) {
  if (error) {
    my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0),
             msg.empty() ? "unknown error occurs" : msg.c_str());
    return;
  }

  ulong err_code = ERR_get_error();

  if (0 == err_code)
    return;

  char buff[256];
  ERR_error_string_n(err_code, buff, sizeof(buff));
  my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0), buff);
  ERR_clear_error();
}

static const std::pair<const char *, algorithm_type> local_algorithm_type[] = {
{"RSA", ALGO_RSA}, {"DSA", ALGO_DSA}, {"DH", ALGO_DH} };

static inline algorithm_type get_algorithm_type(const char *str) {
  if (nullptr == str)
    return ALGO_NONE;

  for (auto it : local_algorithm_type) {
    if (my_strcasecmp(&my_charset_latin1, it.first, str) == 0)
      return it.second;
  }

  return ALGO_NONE;
}

/**
  Read from the memory BIO <key> and place the data in string <out>.

  @param[in] key            the memory BIO pointer.
  @param[out] out            string to store the output data.

  @returns false if success, otherwise true.
*/
static inline bool read_bio_to_str(BIO *key, String *out) {
  if (nullptr == key || nullptr == out)
    return true;

  long int real_len{0};
  char *ptr = nullptr;

  if ((real_len = BIO_get_mem_data(key, &ptr)) > 0) {
    out->append(ptr, real_len, &my_charset_bin);
    return false;
  }

  return true;
}

/**
  Generate an RSA key pair.

  @param[in] rsa_bit            the modulus size.

  @returns false if success, otherwise true.
*/
bool RSA_key::gen_key(int rsa_bit) {
  DBUG_ASSERT(nullptr == m_rsa);
  DBUG_ASSERT(nullptr == m_big_num);

  m_rsa_bits = rsa_bit;

  if ((m_rsa = RSA_new()) == nullptr || (m_big_num = BN_new()) == nullptr)
    return true;

  if (!BN_set_word(m_big_num, RSA_F4))
    return true;

  // RSA_generate_key_ex() returns 1 on success or 0 otherwise!
  if (RSA_generate_key_ex(m_rsa, m_rsa_bits, m_big_num, NULL) == 1)
    return false;

  return true;
}

/**
  Write the RSA private key to string <out>.
  One must ensure that RSA_key::gen_key() has been called and returned success
  before calling this function.

  @param[out] out            string to store the private key.

  @returns false if success, otherwise true.
*/
bool RSA_key::write_priv_key(String *out) const {
  DBUG_ASSERT(nullptr != m_rsa && nullptr != out);

  bool ret{true};

  BIO *priv = BIO_new(BIO_s_mem());
  if (nullptr == priv)
    return true;

  if (PEM_write_bio_RSAPrivateKey(priv, m_rsa, NULL, NULL, 0, NULL, NULL) == 1)
    ret = read_bio_to_str(priv, out);

  BIO_free(priv);
  return ret;
}

/**
  Read the RSA private key from string <str> in PEM format.

  @param[in] str            RSA private key string in PEM format.

  @returns false if success, otherwise true.
*/
bool RSA_key::read_priv_key(String *str) {
  DBUG_ASSERT(nullptr != str);

  BIO *priv = BIO_new_mem_buf(str->ptr(), static_cast<int>(str->length()));
  if (nullptr == priv)
    return true;

  m_priv_key = PEM_read_bio_RSAPrivateKey(priv, &m_priv_key, NULL, NULL);

  BIO_free(priv);

  return (nullptr == m_priv_key);
}

/**
  Read the RSA public key from string <str> in PEM format using an RSA structure.

  @param[in] str            RSA public key string in PEM format.

  @returns false if success, otherwise true.
*/
bool RSA_key::read_pub_key(String *str) {
  DBUG_ASSERT(nullptr != str);

  BIO *pub = BIO_new_mem_buf(str->ptr(), static_cast<int>(str->length()));
  if (nullptr == pub)
    return true;

  m_pub_key = PEM_read_bio_RSA_PUBKEY(pub, &m_pub_key, NULL, NULL);

  BIO_free(pub);

  return (nullptr == m_pub_key);
}

/**
  Extract the public key from the private key and write it to string <out>.
  One must ensure that RSA_key::read_priv_key() has been called and returned
  success before calling this function.

  @param[out] str            string to store the RSA public key.

  @returns false if success, otherwise true.
*/
bool RSA_key::extract_pub_key_from_priv_key(String *out) {
  DBUG_ASSERT(nullptr != m_priv_key && nullptr != out);

  bool ret{true};
  BIO *pub = BIO_new(BIO_s_mem());
  if (nullptr == pub)
    return true;

  if (PEM_write_bio_RSA_PUBKEY(pub, m_priv_key) == 1)
    ret = read_bio_to_str(pub, out);

  BIO_free(pub);
  return ret;
}

/**
  Encrypt the string <str> using the RSA private key <key_str> and store the
  ciphertext in string <out>.

  @param[in] str            string to encrypt.
  @param[in] key_str        RSA private key string in PEM format.
  @param[out] out           string to store the resulting ciphertext.

  @returns false if success, otherwise true.
*/
bool RSA_key::priv_key_encrypt(String *str, String *key_str, String *out) {
  DBUG_ASSERT(nullptr != str && nullptr != key_str && nullptr != out);

  char encrypted_buff[MAX_BUFFER_LEN];

  if (read_priv_key(key_str))
    return true;

  int key_len = RSA_size(m_priv_key);
  if (key_len > MAX_BUFFER_LEN || key_len <= PKCS1_PADDING_LEN) {
    m_error = true;
    m_errmsg.assign("key length is out of supported range");
    return true;
  }

  if (str->length() > ((size_t)0 | (key_len - PKCS1_PADDING_LEN))) {
    m_error = true;
    m_errmsg.assign("plaintext is too long");
    return true;
  }

  int size = RSA_private_encrypt(static_cast<int>(str->length()),
                                 reinterpret_cast<uchar*>(str->ptr()),
                                 reinterpret_cast<uchar*>(encrypted_buff),
                                 m_priv_key, RSA_PKCS1_PADDING);
  if (size >= 0) {
    out->append(encrypted_buff, size, &my_charset_bin);
    return false;
  }

  return true;
}

/**
  Decrypt the ciphertext <crypt_str> using the RSA private key <key_str> and
  write the plaintext to string <out>.

  @param[in] crypt_str       ciphertext.
  @param[in] key_str         RSA private key string in PEM format.
  @param[out] out            string to store the resulting plaintext.

  @returns false if success, otherwise true.
*/
bool RSA_key::priv_key_decrypt(String *crypt_str, String *key_str,
                               String *out) {
  DBUG_ASSERT(nullptr != crypt_str && nullptr != key_str && nullptr != out);

  char plain_text[MAX_BUFFER_LEN];
  if (read_priv_key(key_str))
    return true;

  int key_len = RSA_size(m_priv_key);
  if (key_len > MAX_BUFFER_LEN || crypt_str->length() > MAX_BUFFER_LEN) {
    m_error = true;
    m_errmsg.assign("ciphertext or key string is too long");
    return true;
  }

  int size = RSA_private_decrypt(static_cast<int>(crypt_str->length()),
                                 reinterpret_cast<uchar*>(crypt_str->ptr()),
                                 reinterpret_cast<uchar*>(plain_text),
                                 m_priv_key, RSA_PKCS1_PADDING);
  if (size >= 0) {
    out->append(plain_text, size, &my_charset_bin);
    return false;
  }

  return true;
}

/**
  Encrypt the string <str> using the RSA public key <key_str> and store the
  ciphertext in string <out>.

  @param[in] str            string to encrypt.
  @param[in] key_str        RSA public key string in PEM format.
  @param[out] out           string to store the resulting ciphertext.

  @returns false if success, otherwise true.
*/
bool RSA_key::pub_key_encrypt(String *str, String *key_str, String *out) {
  DBUG_ASSERT(nullptr != str && nullptr != key_str && nullptr != out);

  char encrypted_buff[MAX_BUFFER_LEN];

  if (read_pub_key(key_str))
    return true;

  int key_len = RSA_size(m_pub_key);
  if (key_len > MAX_BUFFER_LEN || key_len <= PKCS1_PADDING_LEN) {
    m_error = true;
    m_errmsg.assign("key length is out of supported range");
    return true;
  }

  if (str->length() > ((size_t)0 | (key_len - PKCS1_PADDING_LEN))) {
    m_error = true;
    m_errmsg.assign("plaintext string is too long");
    return true;
  }

  int size = RSA_public_encrypt(static_cast<int>(str->length()),
                                reinterpret_cast<uchar*>(str->ptr()),
                                reinterpret_cast<uchar*>(encrypted_buff),
                                m_pub_key, RSA_PKCS1_PADDING);
  if (size >= 0) {
    out->append(encrypted_buff, size, &my_charset_bin);
    return false;
  }

  return true;
}

/**
  Decrypt the ciphertext <crypt_str> using the RSA public key <key_str> and
  write the plaintext to string <out>.

  @param[in] crypt_str       ciphertext.
  @param[in] key_str         RSA public key string in PEM format.
  @param[out] out            string to store the resulting plaintext.

  @returns false if success, otherwise true.
*/
bool RSA_key::pub_key_decrypt(String *crypt_str, String *key_str, String *out) {
  DBUG_ASSERT(nullptr != crypt_str && nullptr != key_str && nullptr != out);

  char plain_text[MAX_BUFFER_LEN];
  if (read_pub_key(key_str))
    return true;

  int key_len = RSA_size(m_pub_key);
  if (key_len > MAX_BUFFER_LEN || crypt_str->length() > MAX_BUFFER_LEN) {
    m_error = true;
    m_errmsg.assign("ciphertext or key string is too long");
    return true;
  }

  int size = RSA_public_decrypt(static_cast<int>(crypt_str->length()),
                                reinterpret_cast<uchar*>(crypt_str->ptr()),
                                reinterpret_cast<uchar*>(plain_text),
                                m_pub_key, RSA_PKCS1_PADDING);
  if (size >= 0) {
    out->append(plain_text, size, &my_charset_bin);
    return false;
  }

  return true;
}

/**
  A wrapper function to encrypt the string <str> using the RSA key <key_str>
  and store the ciphertext in string <out>.

  @param[in] str            string to encrypt.
  @param[in] key_str        RSA key string in PEM format.
  @param[out] out           string to store the resulting ciphertext.

  @returns false if success, otherwise true.
*/
bool RSA_key::encrypt(String *str, String *key_str, String *out) {
  DBUG_ASSERT(nullptr != str && nullptr != key_str && nullptr != out);

  if (strstr(key_str->c_ptr_safe(), PRIV_KEY_HEADER))
    return priv_key_encrypt(str, key_str, out);
  else if (strstr(key_str->c_ptr_safe(), PUB_KEY_HEADER))
    return pub_key_encrypt(str, key_str, out);

  m_error = true;
  m_errmsg.assign("incorrect key string");
  return true;
}

/**
  A wrapper function to decrypt the encrypted string <crypt_str> using the RSA
  key <key_str> and write the resulting plaintext to string <out>.

  @param[in] crypt_str       ciphertext.
  @param[in] key_str         RSA public key string in PEM format.
  @param[out] out            string to store the resulting plaintext.

  @returns false if success, otherwise true.
*/
bool RSA_key::decrypt(String *crypt_str, String *key_str, String *out) {
  DBUG_ASSERT(nullptr != crypt_str);
  DBUG_ASSERT(nullptr != key_str);
  DBUG_ASSERT(nullptr != out);

  if (strstr(key_str->c_ptr_safe(), PRIV_KEY_HEADER))
    return priv_key_decrypt(crypt_str, key_str, out);
  else if (strstr(key_str->c_ptr_safe(), PUB_KEY_HEADER))
    return pub_key_decrypt(crypt_str, key_str, out);

  m_error = true;
  m_errmsg.assign("incorrect key string");
  return true;
}

bool Item_func_create_asymmetric_priv_key::resolve_type(THD *) {
  set_data_type_string(MAX_RSA_KEY_LEN, &my_charset_bin);
  maybe_null = true;
  return false;
}

/**
  Create a private key string using the given algorithm and key length
  or DH secret.

  @param[out] str          string to store the resulting private key.

  @returns false if success, otherwise true.
*/
String *Item_func_create_asymmetric_priv_key::val_str(String *str) {
  DBUG_ASSERT(nullptr != str);

  null_value = false;
  str->length(0);

  if (check_and_get_args()) {
    null_value = true;
    return nullptr;
  }

  return create_rsa_priv_key(str);
}

/**
  Check if the key length is out of range according to the algorithm type.

  @returns false if success, otherwise true.
*/
bool Item_func_create_asymmetric_priv_key::check_and_get_key_len() {
  bool ret{false};

  longlong tmp_len = args[1]->val_int();
  m_key_len = static_cast<int>(tmp_len);

  switch (m_algo_type) {
    case ALGO_RSA:
      if (tmp_len < MIN_RSA_KEY_LEN || tmp_len > MAX_RSA_KEY_LEN)
        ret = true;
      break;
    default:
      DBUG_ASSERT(0);  /* never here */
      return true;
  }

  if (ret)
    my_error(ER_DATA_OUT_OF_RANGE, MYF(0), "key length", func_name());

  return ret;
}

bool Item_func_create_asymmetric_priv_key::check_and_get_args() {
  DBUG_ASSERT(2 == arg_count);

  /* Convert to ASCII */
  m_arg0 = args[0]->val_str_ascii(&m_str0);
  m_arg1 = args[1]->val_str(&m_str1);

  /* No error message if either argument is NULL */
  if (nullptr == m_arg0 || nullptr == m_arg1)
    return true;

  m_algo_type = get_algorithm_type(m_arg0->c_ptr_safe());

  if (m_algo_type == ALGO_RSA)
    return check_and_get_key_len();

  my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0),
           "unsupported algorithm");
  return true;
}

/**
  Create an RSA private key string.

  @param[out] out             string to store the resulting RSA private key.

  @returns false if success, otherwise true.
*/
String *Item_func_create_asymmetric_priv_key::create_rsa_priv_key(String *out) {
  DBUG_ASSERT(nullptr != out);

  RSA_key rsa;

  if (rsa.gen_key(m_key_len) || rsa.write_priv_key(out)) {
    push_err_msg(rsa.m_error, rsa.m_errmsg);
    null_value = true;
    return nullptr;
  }

  return out;
}

bool Item_func_create_asymmetric_pub_key::resolve_type(THD *) {
  set_data_type_string(MAX_RSA_KEY_LEN, &my_charset_bin);
  maybe_null = true;
  return false;
}

bool Item_func_create_asymmetric_pub_key::check_and_get_args() {
  DBUG_ASSERT(arg_count == 2);

  /* Convert to ASCII */
  m_arg0 = args[0]->val_str_ascii(&m_str0);
  m_arg1 = args[1]->val_str(&m_str1);

  /* No error message if either argument is NULL */
  if (nullptr == m_arg0 || nullptr == m_arg1)
    return true;

  if (args[0]->result_type() != STRING_RESULT
      || args[1]->result_type() != STRING_RESULT) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), func_name());
    return true;
  }

  if ((m_algo_type = get_algorithm_type(m_arg0->c_ptr_safe())) != ALGO_RSA) {
    my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0),
             "unsupported algorithm");
    return true;
  }

  return false;
}

/**
  Derive a public key from the given private key using the given algorithm.

  @param[out] str          string to store the resulting public key.

  @returns false if success, otherwise true.
*/
String *Item_func_create_asymmetric_pub_key::val_str(String *str) {
  DBUG_ASSERT(nullptr != str);

  null_value = false;
  str->length(0);

  if (check_and_get_args()) {
    null_value = true;
    return nullptr;
  }

  return create_rsa_pub_key(str);
}

/**
  Derive an RSA public key from the given private key.

  @param[out] out          string to store the resulting public key.

  @returns false if success, otherwise true.
*/
String *Item_func_create_asymmetric_pub_key::create_rsa_pub_key(String *out) {
  DBUG_ASSERT(nullptr != out);

  RSA_key rsa;
  if (rsa.read_priv_key(m_arg1) || rsa.extract_pub_key_from_priv_key(out)) {
    push_err_msg(rsa.m_error, rsa.m_errmsg);
    null_value = true;
    return nullptr;
  }

  return out;
}

bool Item_func_asymmetric_encrypt::resolve_type(THD *) {
  set_data_type_string(MAX_RSA_KEY_LEN/8, &my_charset_bin);
  maybe_null = true;
  return false;
}

/**
  Encrypt a string using the given algorithm and key string.

  @param[out] str          string to store the resulting ciphertext.

  @returns false if success, otherwise true.
*/
String *Item_func_asymmetric_encrypt::val_str(String *str) {
  DBUG_ASSERT(nullptr != str);

  null_value = false;
  str->length(0);

  if (check_and_get_args()) {
    null_value = true;
    return nullptr;
  }

  return rsa_asymmetric_encrypt(str);
}

bool Item_func_asymmetric_encrypt::check_and_get_args() {
  DBUG_ASSERT(3 == arg_count);

  if (args[0]->result_type() != STRING_RESULT
      || args[1]->result_type() != STRING_RESULT
      || args[2]->result_type() != STRING_RESULT) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), func_name());
    return true;
  }

  /* Convert to ASCII */
  m_arg0 = args[0]->val_str_ascii(&m_str0);
  m_arg1 = args[1]->val_str(&m_str1);
  m_arg2 = args[2]->val_str(&m_str2);

  /* No error message if any argument is NULL */
  if (nullptr == m_arg0 || nullptr == m_arg1 || nullptr == m_arg2)
    return true;

  m_algo_type = get_algorithm_type(m_arg0->c_ptr_safe());
  if (ALGO_RSA == m_algo_type)
    return false;

  my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0),
           "unsupported algorithm");
  return true;
}

/**
  Encrypt a string using the RSA algorithm and key string.

  @param[out] out          string to store the resulting ciphertext.

  @returns false if success, otherwise true.
*/
String *Item_func_asymmetric_encrypt::rsa_asymmetric_encrypt(String *out) {
  DBUG_ASSERT(nullptr != out);

  RSA_key rsa;
  if (rsa.encrypt(m_arg1, m_arg2, out)) {
    push_err_msg(rsa.m_error, rsa.m_errmsg);
    null_value = true;
    return nullptr;
  }

  return out;
}

bool Item_func_asymmetric_decrypt::resolve_type(THD *) {
  set_data_type_string(MAX_RSA_KEY_LEN/8, &my_charset_bin);
  maybe_null = true;
  return false;
}

/**
  Decrypt an encrypted string using the given algorithm and key string.

  @param[out] str          string to store the resulting plaintext.

  @returns false if success, otherwise true.
*/
String *Item_func_asymmetric_decrypt::val_str(String *str) {
  DBUG_ASSERT(nullptr != str);

  null_value = false;
  str->length(0);

  if (check_and_get_args()) {
    null_value = true;
    return nullptr;
  }

  return rsa_asymmetric_decrypt(str);
}

bool Item_func_asymmetric_decrypt::check_and_get_args() {
  DBUG_ASSERT(3 == arg_count);

  if (args[0]->result_type() != STRING_RESULT
      || args[1]->result_type() != STRING_RESULT
      || args[2]->result_type() != STRING_RESULT) {
    my_error(ER_WRONG_PARAMETERS_TO_NATIVE_FCT, MYF(0), func_name());
    return true;
  }

  /* Convert to ASCII */
  m_arg0 = args[0]->val_str_ascii(&m_str0);
  m_arg1 = args[1]->val_str(&m_str1);
  m_arg2 = args[2]->val_str(&m_str2);

  /* No error message if any argument is NULL */
  if (nullptr == m_arg0 || nullptr == m_arg1 || nullptr == m_arg2)
    return true;

  m_algo_type = get_algorithm_type(m_arg0->c_ptr_safe());
  if (ALGO_RSA == m_algo_type)
    return false;

  my_error(ER_ASYMMETRIC_ENCRYPTION_FUNC_FAILED, MYF(0),
           "unsupported algorithm");
  return true;
}

/**
  Decrypt an encrypted string using the RSA algorithm and key string.

  @param[out] out          string to store the resulting plaintext.

  @returns false if success, otherwise true.
*/
String *Item_func_asymmetric_decrypt::rsa_asymmetric_decrypt(String *out) {
  DBUG_ASSERT(nullptr != out);

  RSA_key rsa;
  if (rsa.decrypt(m_arg1, m_arg2, out)) {
    push_err_msg(rsa.m_error, rsa.m_errmsg);
    null_value = true;
    return nullptr;
  }

  return out;
}
