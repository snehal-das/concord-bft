// UTT Client API
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

#include "PrivacyService.hpp"
#include <storage/FileBasedUserStorage.hpp>
#include <iostream>
#include <utt-client-api/ClientApi.hpp>
#include <utils/crypto.hpp>

using namespace utt::client::utils::crypto;
using namespace ::vmware::concord::privacy::wallet::api::v1;
namespace utt::walletservice {
const std::string PrivacyWalletServiceImpl::wallet_db_path = "wallet-db";

PrivacyWalletService::PrivacyWalletService() {
  utt::client::Initialize();
  privacy_wallet_service_ = std::make_unique<PrivacyWalletServiceImpl>();
}

PrivacyWalletService::~PrivacyWalletService() { std::cout << " Destroying privacy wallet service...\n"; }

void PrivacyWalletService::StartServer(const std::string& url) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(url, grpc::InsecureServerCredentials());
  builder.RegisterService(privacy_wallet_service_.get());
  grpc_server_ = builder.BuildAndStart();
  std::cout << "Server listening on " << url << std::endl;
}

void PrivacyWalletService::Wait() {
  // if (grpc_server_ != nullptr) {
  std::cout << "wait for server to terminate" << std::endl;
  grpc_server_->Wait();
  std::cout << "server wait terminated" << std::endl;
  //}
}
void PrivacyWalletService::Shutdown() {
  if (grpc_server_ != nullptr) {
    std::cout << "server shutdown" << std::endl;
    grpc_server_->Shutdown();
    std::cout << "server shutdown complete.." << std::endl;
  }
}
PrivacyWalletServiceImpl::PrivacyWalletServiceImpl() {
  try {
    wallet_ = Wallet::recoverFromStorage(PrivacyWalletServiceImpl::wallet_db_path);
  } catch (std::exception& e) {
    std::cout << "Caught exception: " << e.what() << std::endl;
    std::exit(0);
  } catch (...) {
    std::cout << "storage is corrupted, unable to restore the wallet service from the given storage" << std::endl;
    std::exit(0);
  }

  if (wallet_) {
    std::cout << "wallet service recovered from storage" << std::endl;
  }
}
::grpc::Status PrivacyWalletServiceImpl::PrivacyWalletService(::grpc::ServerContext* context,
                                                              const PrivacyWalletRequest* request,
                                                              PrivacyWalletResponse* response) {
  auto status = grpc::Status::OK;
  try {
    if (request->has_privacy_app_config()) {
      std::cout << "Processing privacy app config request" << request->ShortDebugString() << std::endl;
      return handleApplicationConfigRequest(context, request, response);
    } else if (request->has_privacy_wallet_config_request()) {
      // dont log sensitive key information, okay to log public key though..
      auto walletCfgRequest = request->privacy_wallet_config_request();
      std::cout << "Processing wallet config request for UserID: " << walletCfgRequest.user_id() << std::endl;
      std::cout << "User public key: " << walletCfgRequest.public_key() << std::endl;
      std::cout << "Configuration length: " << walletCfgRequest.public_application_config().length() << std::endl;
      return handleWalletConfigRequest(context, request, response);
    } else if (request->has_user_registration_request()) {
      std::cout << "Processing user registration request" << request->ShortDebugString() << std::endl;
      return handleUserRegistrationRequest(context, request, response);
    } else if (request->has_user_registration_update_request()) {
      std::cout << "Processing user registration update request" << request->ShortDebugString() << std::endl;
      return handleUserRegistrationUpdateRequest(context, request, response);
    } else if (request->has_claim_coins_request()) {
      std::cout << "Processing claim request: " << TxType_Name(request->claim_coins_request().type()) << std::endl;
      return handleUserClaimCoinsRequest(context, request, response);
    } else if (request->has_generate_mint_tx_request()) {
      std::cout << "Processing mint request" << request->ShortDebugString() << std::endl;
      return handleUserMintRequest(context, request, response);
    } else if (request->has_generate_burn_tx_request()) {
      std::cout << "Processing burn request" << request->ShortDebugString() << std::endl;
      return handleUserBurnRequest(context, request, response);
    } else if (request->has_generate_transfer_tx_request()) {
      // on real deployment it might be good idea not to expose receiver information or masquerade it..
      std::cout << "Processing transfer request" << request->ShortDebugString() << std::endl;
      return handleUserTransferRequest(context, request, response);
    } else if (request->has_get_state_request()) {
      std::cout << "Processing state request" << std::endl;
      return handleGetStateRequest(context, request, response);
    } else if (request->has_set_app_data_request()) {
      std::cout << "Processing set application data request" << std::endl;
      return handleSetAppDataRequest(context, request, response);
    } else if (request->has_get_app_data_request()) {
      std::cout << "Processing get application data request" << std::endl;
      return handleGetAppDataRequest(context, request, response);
    } else {
      std::cout << "unknown request: " << request->DebugString() << std::endl;
      status = grpc::Status(grpc::StatusCode::UNKNOWN, "Unknown error");
    }
    return status;
  } catch (const std::exception& e) {
    std::cout << "Failed to handle request:" << e.what() << std::endl;
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, e.what());
  }
}

// @FIXME - make this asynchronous..
::grpc::Status PrivacyWalletServiceImpl::handleApplicationConfigRequest(::grpc::ServerContext* /*context*/,
                                                                        const PrivacyWalletRequest* request,
                                                                        PrivacyWalletResponse* response) {
  auto status = grpc::Status::OK;
  // Generate a privacy config for a N=4 replica system tolerating F=1 failures
  utt::client::ConfigInputParams params;
  for (auto i = 0; i < request->privacy_app_config().validatorpublickey_size(); i++) {
    params.validatorPublicKeys.emplace_back(request->privacy_app_config().validatorpublickey(i));
  }
  params.threshold = uint16_t(request->privacy_app_config().numvalidators());
  params.useBudget = request->privacy_app_config().budget();  // F + 1
  auto config = utt::client::generateConfig(params);
  if (config.empty()) {
    std::cout << "failed to generate config!" << std::endl;
    status = grpc::Status(grpc::StatusCode::INTERNAL, "Failed to create config");
  } else {
    auto configResp = response->mutable_privacy_app_config_response();
    configResp->set_configuration(config.data(), config.size());
    std::cout << "Successfully send back application config!" << std::endl;
  }
  return status;
}

::grpc::Status PrivacyWalletServiceImpl::handleWalletConfigRequest(::grpc::ServerContext* /*context*/,
                                                                   const PrivacyWalletRequest* request,
                                                                   PrivacyWalletResponse* response) {
  if (wallet_) {
    std::string err_msg = "wallet is already configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, err_msg);
  }
  auto status = grpc::Status::OK;
  const auto& req = request->privacy_wallet_config_request();
  auto public_key = req.public_key();
  auto userId = req.user_id();
  utt::PublicConfig publicConfig(req.public_application_config().begin(), req.public_application_config().end());
  storage_ = std::make_shared<utt::client::FileBasedUserStorage>(PrivacyWalletServiceImpl::wallet_db_path);
  wallet_ = std::make_unique<Wallet>(userId, req.private_key(), public_key, storage_, publicConfig);
  auto resp = response->mutable_privacy_wallet_config_response();
  resp->set_succ(true);
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleUserRegistrationRequest(::grpc::ServerContext*,
                                                                       const PrivacyWalletRequest*,
                                                                       PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }
  if (wallet_->isRegistered()) {
    std::string err_msg = "user is already registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, err_msg);
  }
  auto registration_input = wallet_->generateRegistrationInput();
  if (!registration_input.has_value()) {
    std::string err_msg = "error while generating registration input";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::INTERNAL, err_msg);
  }
  auto reg_response = response->mutable_user_registration_response();
  reg_response->set_rcm1(registration_input->rcm1.data(), registration_input->rcm1.size());
  reg_response->set_rcm1_sig(registration_input->rcm1_sig.data(), registration_input->rcm1_sig.size());
  reg_response->set_pid(wallet_->getUserId());
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleUserRegistrationUpdateRequest(::grpc::ServerContext*,
                                                                             const PrivacyWalletRequest* request,
                                                                             PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  if (wallet_->isRegistered()) {
    std::string err_msg = "user is already registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, err_msg);
  }

  const auto& req = request->user_registration_update_request();
  auto res = wallet_->updateRegistrationCommitment({req.rcm_sig().begin(), req.rcm_sig().end()},
                                                   {req.s2().begin(), req.s2().end()});
  if (!res) {
    std::string err_msg = "unable to update registration data";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::ABORTED, err_msg);
  }
  auto resp = response->mutable_user_registration_update_response();
  resp->set_succ(true);
  return grpc::Status::OK;
}

std::pair<utt::Transaction, utt::TxOutputSigs> PrivacyWalletServiceImpl::buildClaimCoinsData(
    const ClaimCoinsRequest& req) {
  utt::Transaction::Type type{utt::Transaction::Type::Undefined};
  switch (req.type()) {
    case TxType::MINT:
      type = utt::Transaction::Type::Mint;
      break;
    case TxType::BURN:
      type = utt::Transaction::Type::Burn;
      break;
    case TxType::TRANSFER:
      type = utt::Transaction::Type::Transfer;
      break;
    case TxType::BUDGET:
      type = utt::Transaction::Type::Budget;
      break;
    default:
      std::cout << "invalid transaction type" << std::endl;
      throw std::runtime_error("invalid transaction type");
  }
  utt::TxOutputSigs sigs{static_cast<size_t>(req.sigs().size())};
  for (int i = 0; i < req.sigs().size(); i++) {
    sigs[static_cast<size_t>(i)] = {req.sigs(i).begin(), req.sigs(i).end()};
  }
  return {utt::Transaction{type, {req.tx().begin(), req.tx().end()}, static_cast<uint32_t>(sigs.size())}, sigs};
}
::grpc::Status PrivacyWalletServiceImpl::handleUserClaimCoinsRequest(::grpc::ServerContext*,
                                                                     const PrivacyWalletRequest* request,
                                                                     PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }
  if (!wallet_->isRegistered()) {
    std::string err_msg = "user is not registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }
  try {
    auto data = buildClaimCoinsData(request->claim_coins_request());
    auto res = wallet_->claimCoins(data.first, data.second);
    if (!res) {
      std::string err_msg = "unable to claim coins";
      std::cout << err_msg << std::endl;
      response->set_err(err_msg);
      return grpc::Status(grpc::StatusCode::ABORTED, err_msg);
    }
  } catch (const libutt::api::operations::InvalidCoinsInTransfer& e) {
    std::cout << e.what() << std::endl;
    response->mutable_claim_coins_response()->set_warning(e.what());
  } catch (const std::exception& e) {
    std::cout << e.what() << std::endl;
    response->set_err(e.what());
    return grpc::Status(grpc::StatusCode::ABORTED, e.what());
  }
  if (response) {
    response->mutable_claim_coins_response()->set_succ(true);
  }
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleUserMintRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest* request,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  if (!wallet_->isRegistered()) {
    std::string err_msg = "user is not registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  auto& mint_req = request->generate_mint_tx_request();
  auto res = wallet_->generateMintTx(mint_req.amount());
  auto tx_resp = response->mutable_generate_tx_response();
  tx_resp->set_tx(res.data_.data(), res.data_.size());
  tx_resp->set_final(true);
  tx_resp->set_num_of_output_coins(1);
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleUserBurnRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest* request,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  if (!wallet_->isRegistered()) {
    std::string err_msg = "user is not registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  auto& burn_req = request->generate_burn_tx_request();
  auto res = wallet_->generateBurnTx(burn_req.amount());
  auto tx_resp = response->mutable_generate_tx_response();
  tx_resp->set_tx(res.requiredTx_.data_.data(), res.requiredTx_.data_.size());
  tx_resp->set_final(res.isFinal_);
  tx_resp->set_num_of_output_coins(res.requiredTx_.numOutputs_);
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleUserTransferRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest* request,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  if (!wallet_->isRegistered()) {
    std::string err_msg = "user is not registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  auto& transfer_req = request->generate_transfer_tx_request();
  auto res = wallet_->generateTransferTx(
      transfer_req.amount(),
      {transfer_req.recipient_pid().begin(), transfer_req.recipient_pid().end()},
      {transfer_req.recipient_public_key().begin(), transfer_req.recipient_public_key().end()});
  auto tx_resp = response->mutable_generate_tx_response();
  tx_resp->set_tx(res.requiredTx_.data_.data(), res.requiredTx_.data_.size());
  tx_resp->set_final(res.isFinal_);
  tx_resp->set_num_of_output_coins(res.requiredTx_.numOutputs_);
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleGetStateRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest*,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  if (!wallet_) {
    std::string err_msg = "wallet is not configured";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  if (!wallet_->isRegistered()) {
    std::string err_msg = "user is not registered";
    std::cout << err_msg << std::endl;
    response->set_err(err_msg);
    return grpc::Status(grpc::StatusCode::NOT_FOUND, err_msg);
  }

  auto state_resp = response->mutable_get_state_response();
  state_resp->set_budget(wallet_->getBudget());
  state_resp->set_balance(wallet_->getBalance());
  state_resp->set_user_id(wallet_->getUserId());
  for (const auto& coin : wallet_->getCoinsDescriptors()) {
    (*(state_resp->mutable_coins()))[coin.nullifier_] = coin.value_;
  }
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleSetAppDataRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest* request,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  auto& set_app_data_req = request->set_app_data_request();
  for (int i = 0; i < set_app_data_req.keys().size(); i++) {
    storage_->setAppData(set_app_data_req.keys(i), set_app_data_req.values(i));
  }
  if (response) {
    auto set_app_data_resp = response->mutable_set_app_data_response();
    set_app_data_resp->set_succ(true);
  }
  return grpc::Status::OK;
}

::grpc::Status PrivacyWalletServiceImpl::handleGetAppDataRequest(
    ::grpc::ServerContext*,
    const ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletRequest* request,
    ::vmware::concord::privacy::wallet::api::v1::PrivacyWalletResponse* response) {
  if (response) {
    auto& get_app_data_req = request->get_app_data_request();
    auto get_app_data_resp = response->mutable_get_app_data_response();
    for (int i = 0; i < get_app_data_req.keys().size(); i++) {
      get_app_data_resp->add_values(storage_->getAppData(get_app_data_req.keys(i)));
    }
  }
  return grpc::Status::OK;
}

}  // namespace utt::walletservice
