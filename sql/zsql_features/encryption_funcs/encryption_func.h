#ifndef ENCRYPTION_FUNC_INCLUDED
#define ENCRYPTION_FUNC_INCLUDED

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <string>
#include "sql/item.h"
#include "sql/item_strfunc.h"


enum algorithm_type { ALGO_NONE = 0, ALGO_RSA, ALGO_DSA, ALGO_DH };

/**
  Class to support RSA algorithm.
*/
class RSA_key final {
 public:
  RSA_key() { }

  ~RSA_key() {
    if (nullptr != m_rsa) {
      RSA_free(m_rsa);
      m_rsa = nullptr;
    }

    if (nullptr != m_pub_key) {
      RSA_free(m_pub_key);
      m_pub_key = nullptr;
    }

    if (nullptr != m_priv_key) {
      RSA_free(m_priv_key);
      m_priv_key = nullptr;
    }

    if (nullptr != m_big_num) {
      BN_free(m_big_num);
      m_big_num = nullptr;
    }
  }

  bool gen_key(int rsa_bit);

  bool write_priv_key(String *out) const;
  bool read_priv_key(String *str);
  bool read_pub_key(String *str);

  bool extract_pub_key_from_priv_key(String *out);

  bool encrypt(String *crypt_str, String *key_str, String *out);
  bool decrypt(String *str, String *key_str, String *out);

 private:
  RSA_key(const RSA_key&) = delete;
  RSA_key &operator=(const RSA_key&) = delete;

  bool priv_key_encrypt(String *str, String *key_str, String *out);
  bool priv_key_decrypt(String *crypt_str, String *key_str, String *out);
  bool pub_key_encrypt(String *str, String *key_str, String *out);
  bool pub_key_decrypt(String *crypt_str, String *key_str, String *out);

 public:
  bool m_error{false};
  std::string m_errmsg;

 private:
  RSA *m_rsa{nullptr};
  RSA *m_pub_key{nullptr};
  RSA *m_priv_key{nullptr};
  BIGNUM *m_big_num{nullptr};
  int m_rsa_bits{0};
};

class Item_func_create_asymmetric_priv_key final : public Item_str_func {
 public:
  Item_func_create_asymmetric_priv_key(const POS &pos, Item *arg0, Item *arg1)
      : Item_str_func(pos, arg0, arg1)
  { }

  String *val_str(String *) override;
  bool resolve_type(THD *) override;
  const char *func_name() const override { return "create_asymmetric_priv_key"; }

 private:
  String *create_rsa_priv_key(String *out);

  bool check_and_get_args();
  bool check_and_get_key_len();

 private:
  String m_str0, m_str1, *m_arg0{&m_str0}, *m_arg1{&m_str1};
  int m_key_len{0};
  algorithm_type m_algo_type{ALGO_NONE};
};

class Item_func_create_asymmetric_pub_key final : public Item_str_func {
 public:
  Item_func_create_asymmetric_pub_key(const POS &pos, Item *arg0, Item *arg1)
      : Item_str_func(pos, arg0, arg1) {}

  String *val_str(String *) override;
  bool resolve_type(THD *) override;
  const char *func_name() const override { return "create_asymmetric_pub_key"; }

 private:
  String *create_rsa_pub_key(String *out);

  bool check_and_get_args();

 private:
  String m_str0, m_str1;
  String *m_arg0{&m_str0}, *m_arg1{&m_str1};
  algorithm_type m_algo_type{ALGO_NONE};
};

class Item_func_asymmetric_encrypt final : public Item_str_func {
 public:
  Item_func_asymmetric_encrypt(const POS &pos, Item *arg0,
                               Item *arg1, Item *arg2)
      : Item_str_func(pos, arg0, arg1, arg2) {}

  String *val_str(String *) override;
  bool resolve_type(THD *) override;
  const char *func_name() const override { return "asymmetric_encrypt"; }

 private:
  String *rsa_asymmetric_encrypt(String *str);
  bool check_and_get_args();

 private:
  String m_str0, m_str1, m_str2;
  String *m_arg0{&m_str0}, *m_arg1{&m_str1}, *m_arg2{&m_str2};
  algorithm_type m_algo_type{ALGO_NONE};
};

class Item_func_asymmetric_decrypt final : public Item_str_func {
 public:
  Item_func_asymmetric_decrypt(const POS &pos, Item *arg0,
                               Item *arg1, Item *arg2)
      : Item_str_func(pos, arg0, arg1, arg2) {}

  String *val_str(String *) override;
  bool resolve_type(THD *) override;
  const char *func_name() const override { return "asymmetric_decrypt"; }

 private:
  String *rsa_asymmetric_decrypt(String *str);
  bool check_and_get_args();

 private:
  String m_str0, m_str1, m_str2;
  String *m_arg0{&m_str0}, *m_arg1{&m_str1}, *m_arg2{&m_str2};
  algorithm_type m_algo_type{ALGO_NONE};
};

#endif
