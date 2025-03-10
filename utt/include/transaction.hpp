// UTT
//
// Copyright (c) 2020-2022 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0
// License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the sub-component's license, as noted in the LICENSE
// file.

#pragma once
#include "client.hpp"
#include "coin.hpp"
#include "UTTParams.hpp"
#include "coinsSigner.hpp"
#include "client.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace libutt::api::operations {
class Transaction;
}

std::ostream& operator<<(std::ostream& out, const libutt::api::operations::Transaction& tx);
std::istream& operator>>(std::istream& in, libutt::api::operations::Transaction& tx);

namespace libutt {
class IEncryptor;
}  // namespace libutt

namespace libutt::api::operations {
class Transaction {
  /**
   * @brief A transaction represents a transfer of UTT coin(s) from a sender account c1 to other account(s) [c2, ...,
   * cn]. Notice, that a self transaction is also valid
   *
   */
 public:
  /**
   * @brief Construct a new Transaction object
   *
   * @param p The shared global UTT parameters
   * @param client The client that creates the transaction
   * @param input_coins The transaction's input UTT coins
   * @param budget_coin An optional budget coin (self transaction doesn't require a budget coin)
   * @param recipients A vector of recipients, each recipient is represented as <string id,numeric id>
   * @param encryptor An object that responsible for encrypting some of the transaction data for the given recipients
   * (see libutt/include/DataUtils.h)
   */
  Transaction(const UTTParams& p,
              const Client& client,
              const std::vector<Coin>& input_coins,
              const std::optional<Coin>& budget_coin,
              const std::vector<std::tuple<std::string, uint64_t>>& recipients,
              const IEncryptor& encryptor);
  Transaction();
  Transaction(const Transaction&);
  Transaction& operator=(const Transaction&);
  ~Transaction();

  Transaction(Transaction&&);
  Transaction& operator=(Transaction&&);
  /**
   * @brief Get the transaction's nullifiers
   *
   * @return std::vector<std::string>
   */
  std::vector<std::string> getNullifiers() const;

  /**
   * @brief Get the number of output coins in the transaction
   *
   * @return uint32_t
   */
  uint32_t getNumOfOutputCoins() const;

  bool hasBudgetCoin() const;
  uint64_t getBudgetExpirationDate() const;

 private:
  friend class libutt::api::CoinsSigner;
  friend class libutt::api::Client;
  friend std::ostream& ::operator<<(std::ostream& out, const libutt::api::operations::Transaction& tx);
  friend std::istream& ::operator>>(std::istream& in, libutt::api::operations::Transaction& tx);
  struct Impl;
  Impl* pImpl_;
};

class InvalidCoinsInTransfer : public std::exception {
 public:
  explicit InvalidCoinsInTransfer(const std::string& what) : msg(what){};
  virtual const char* what() const noexcept override { return msg.c_str(); }

 private:
  std::string msg;
};
}  // namespace libutt::api::operations
