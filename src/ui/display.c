/*****************************************************************************
 *   Ledger App Boilerplate.
 *   (c) 2020 Ledger SAS.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *****************************************************************************/

#pragma GCC diagnostic ignored "-Wformat-invalid-specifier"  // snprintf
#pragma GCC diagnostic ignored "-Wformat-extra-args"         // snprintf

#include <stdbool.h>  // bool
#include <string.h>   // memset

#include "os.h"
#include "ux.h"
#include "glyphs.h"

#include "display.h"
#include "constants.h"
#include "../globals.h"
#include "../io.h"
#include "../sw.h"
#include "../address.h"
#include "action/validate.h"
#include "../transaction/types.h"
#include "../common/bip32.h"
#include "../common/format.h"

/////////////////////////
#include "shared_context.h"
#include "../types.h"
#include "utils2.h"
#include "../crypto.h"
/////////////////////////

static action_validate_cb g_validate_callback;
static char g_amount[30];
static char g_bip32_path[60];
static char g_address[43];

// Step with icon and text
UX_STEP_NOCB(ux_display_confirm_addr_step, pn, {&C_icon_eye, "Confirm Address"});
// Step with title/text for BIP32 path
UX_STEP_NOCB(ux_display_path_step,
             bnnn_paging,
             {
                 .title = "Path",
                 .text = g_bip32_path,
             });
// Step with title/text for address
UX_STEP_NOCB(ux_display_address_step,
             bnnn_paging,
             {
                 .title = "Address",
                 .text = g_address,
             });
// Step with approve button
UX_STEP_CB(ux_display_approve_step,
           pb,
           (*g_validate_callback)(true),
           {
               &C_icon_validate_14,
               "Approve",
           });
// Step with reject button
UX_STEP_CB(ux_display_reject_step,
           pb,
           (*g_validate_callback)(false),
           {
               &C_icon_crossmark,
               "Reject",
           });

// FLOW to display address and BIP32 path:
// #1 screen: eye icon + "Confirm Address"
// #2 screen: display BIP32 Path
// #3 screen: display address
// #4 screen: approve button
// #5 screen: reject button
UX_FLOW(ux_display_pubkey_flow,
        &ux_display_confirm_addr_step,
        &ux_display_path_step,
        &ux_display_address_step,
        &ux_display_approve_step,
        &ux_display_reject_step);

int ui_display_address() {
    if (G_context.req_type != CONFIRM_ADDRESS || G_context.state != STATE_NONE) {
        G_context.state = STATE_NONE;
        return io_send_sw(SW_BAD_STATE);
    }

    memset(g_bip32_path, 0, sizeof(g_bip32_path));
    if (!bip32_path_format(G_context.bip32_path,
                           G_context.bip32_path_len,
                           g_bip32_path,
                           sizeof(g_bip32_path))) {
        return io_send_sw(SW_DISPLAY_BIP32_PATH_FAIL);
    }

    memset(g_address, 0, sizeof(g_address));
    uint8_t address[ADDRESS_LEN] = {0};
    if (!address_from_pubkey(G_context.pk_info.raw_public_key, address, sizeof(address))) {
        return io_send_sw(SW_DISPLAY_ADDRESS_FAIL);
    }
    snprintf(g_address, sizeof(g_address), "0x%.*H", sizeof(address), address);

    g_validate_callback = &ui_action_validate_pubkey;

    ux_flow_init(0, ux_display_pubkey_flow, NULL);

    return 0;
}

// Step with icon and text
UX_STEP_NOCB(ux_display_review_step,
             pnn,
             {
                 &C_icon_eye,
                 "Review",
                 "Transaction",
             });
// Step with title/text for amount
UX_STEP_NOCB(ux_display_amount_step,
             bnnn_paging,
             {
                 .title = "Amount",
                 .text = g_amount,
             });

// FLOW to display transaction information:
// #1 screen : eye icon + "Review Transaction"
// #2 screen : display amount
// #3 screen : display destination address
// #4 screen : approve button
// #5 screen : reject button
UX_FLOW(ux_display_transaction_flow,
        &ux_display_review_step,
        &ux_display_address_step,
        &ux_display_amount_step,
        &ux_display_approve_step,
        &ux_display_reject_step);

int ui_display_transaction() {
    if (G_context.req_type != CONFIRM_TRANSACTION || G_context.state != STATE_PARSED) {
        G_context.state = STATE_NONE;
        return io_send_sw(SW_BAD_STATE);
    }

    memset(g_amount, 0, sizeof(g_amount));
    char amount[30] = {0};
    if (!format_fpu64(amount,
                      sizeof(amount),
                      G_context.tx_info.transaction.value,
                      EXPONENT_SMALLEST_UNIT)) {
        return io_send_sw(SW_DISPLAY_AMOUNT_FAIL);
    }
    snprintf(g_amount, sizeof(g_amount), "CFX %.*s", sizeof(amount), amount);
    PRINTF("Amount: %s\n", g_amount);

    memset(g_address, 0, sizeof(g_address));
    snprintf(g_address, sizeof(g_address), "0x%.*H", ADDRESS_LEN, G_context.tx_info.transaction.to);

    g_validate_callback = &ui_action_validate_transaction;

    ux_flow_init(0, ux_display_transaction_flow, NULL);

    return 0;
}









////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void prepareFeeDisplay();
void prepareNetworkDisplay();
void ux_approve_tx(bool fromPlugin);

void prepareDisplayTransaction() {
    char displayBuffer[50];

    // Store the hash
    cx_hash((cx_hash_t *) &global_sha3,
            CX_LAST,
            G_context.tx_info.m_hash,
            0,
            G_context.tx_info.m_hash,
            32);

    PRINTF("Hash: %.*H\n", INT256_LENGTH, G_context.tx_info.m_hash);

    // Prepare destination address to display
    if (G_context.tx_content.destinationLength != 0) {
        getEthDisplayableAddress(G_context.tx_content.destination, displayBuffer, sizeof(displayBuffer), &global_sha3, 1); // TODO
        strlcpy(strings.common.fullAddress, displayBuffer, sizeof(strings.common.fullAddress));
    } else {
        strcpy(strings.common.fullAddress, "Contract");
    }

    // Prepare amount to display
    amountToString(G_context.tx_content.value.value, G_context.tx_content.value.length, WEI_TO_ETHER, "CFX", displayBuffer, sizeof(displayBuffer));
    strlcpy(strings.common.fullAmount, displayBuffer, sizeof(strings.common.fullAmount));

    // Prepare nonce to display
    uint256_t nonce;
    convertUint256BE(G_context.tx_content.nonce.value, G_context.tx_content.nonce.length, &nonce);
    tostring256(&nonce, 10, displayBuffer, sizeof(displayBuffer));
    strlcpy(strings.common.nonce, displayBuffer, sizeof(strings.common.nonce));

    // Compute maximum fee
    prepareFeeDisplay();
    PRINTF("Fees displayed: %s\n", strings.common.maxFee);

    // Prepare chainID field
    prepareNetworkDisplay();
    PRINTF("Network: %s\n", strings.common.network_name);

    g_validate_callback = &ui_action_validate_transaction_2;
    ux_approve_tx(false);
}

void prepareNetworkDisplay() {
    uint64_t chain_id = u64_from_BE(G_context.tx_content.chainID.value, G_context.tx_content.chainID.length);

    switch (chain_id) {
        case CONFLUX_MAINNET_CHAINID:
            strlcpy(strings.common.network_name, "mainnet", sizeof(strings.common.network_name));
            break;

        case CONFLUX_TESTNET_CHAINID:
            strlcpy(strings.common.network_name, "testnet", sizeof(strings.common.network_name));
            break;

        default:
            u64_to_string(chain_id, strings.common.network_name, sizeof(strings.common.network_name));
    }
}

// Convert `BEgasPrice` and `BEgasLimit` to Uint256 and then stores the multiplication of both in
// `output`.
static void computeFees(txInt256_t *BEgasPrice, txInt256_t *BEgasLimit, uint256_t *output) {
    uint256_t gasPrice = {0};
    uint256_t gasLimit = {0};

    PRINTF("Gas price %.*H\n", BEgasPrice->length, BEgasPrice->value);
    PRINTF("Gas limit %.*H\n", BEgasLimit->length, BEgasLimit->value);
    convertUint256BE(BEgasPrice->value, BEgasPrice->length, &gasPrice);
    convertUint256BE(BEgasLimit->value, BEgasLimit->length, &gasLimit);
    mul256(&gasPrice, &gasLimit, output);
}

static void feesToString(uint256_t *rawFee, char *displayBuffer, uint32_t displayBufferSize) {
    char *feeTicker = "CFX";
    uint8_t tickerOffset = 0;
    uint32_t i;

    tostring256(rawFee, 10, (char *) (G_io_apdu_buffer + 100), 100);
    i = 0;
    while (G_io_apdu_buffer[100 + i]) {
        i++;
    }
    adjustDecimals((char *) (G_io_apdu_buffer + 100),
                   i,
                   (char *) G_io_apdu_buffer,
                   100,
                   WEI_TO_ETHER);
    i = 0;
    tickerOffset = 0;
    memset(displayBuffer, 0, displayBufferSize);
    while (feeTicker[tickerOffset]) {
        displayBuffer[tickerOffset] = feeTicker[tickerOffset];
        tickerOffset++;
    }
    while (G_io_apdu_buffer[i]) {
        displayBuffer[tickerOffset + i] = G_io_apdu_buffer[i];
        i++;
    }
    displayBuffer[tickerOffset + i] = '\0';
}

void prepareAndCopyFees(txInt256_t *BEGasPrice,
                        txInt256_t *BEGasLimit,
                        char *displayBuffer,
                        uint32_t displayBufferSize) {
    uint256_t rawFee = {0};
    computeFees(BEGasPrice, BEGasLimit, &rawFee);
    feesToString(&rawFee, displayBuffer, displayBufferSize);
}

void prepareFeeDisplay() {
    prepareAndCopyFees(&G_context.tx_content.gasprice,
                       &G_context.tx_content.gaslimit,
                       strings.common.maxFee,
                       sizeof(strings.common.maxFee));
}

UX_STEP_NOCB(ux_approval_review_step,
    pnn,
    {
      &C_icon_eye,
      "Review",
      "transaction",
    });

UX_STEP_NOCB(
    ux_approval_amount_step,
    bnnn_paging,
    {
      .title = "Amount",
      .text = strings.common.fullAmount
    });
UX_STEP_NOCB(
    ux_approval_nonce_step,
    bnnn_paging,
    {
      .title = "Nonce",
      .text = strings.common.nonce
    });
UX_STEP_NOCB(
    ux_approval_address_step,
    bnnn_paging,
    {
      .title = "Address",
      .text = strings.common.fullAddress,
    });

UX_STEP_NOCB(
    ux_approval_fees_step,
    bnnn_paging,
    {
      .title = "Max Fees",
      .text = strings.common.maxFee,
    });
UX_STEP_NOCB(
    ux_approval_network_step,
    bnnn_paging,
    {
      .title = "Network",
      .text = strings.common.network_name,
    });

UX_STEP_CB(
    ux_approval_accept_step,
    pbb,
    (*g_validate_callback)(true),
    {
      &C_icon_validate_14,
      "Accept",
      "and send",
    });
UX_STEP_CB(
    ux_approval_reject_step,
    pb,
    (*g_validate_callback)(false),
    {
      &C_icon_crossmark,
      "Reject",
    });

const ux_flow_step_t *ux_approval_tx_flow[15];

void ux_approve_tx(bool fromPlugin) {
    int step = 0;
    ux_approval_tx_flow[step++] = &ux_approval_review_step;

    // if (!fromPlugin && G_context.tx_content.dataPresent && !N_storage.contractDetails) {
    //     ux_approval_tx_flow[step++] = &ux_approval_blind_signing_warning_step;
    // }

    // if (fromPlugin) {
    //     // Add the special dynamic display logic
    //     ux_approval_tx_flow[step++] = &ux_plugin_approval_id_step;
    //     ux_approval_tx_flow[step++] = &ux_plugin_approval_before_step;
    //     ux_approval_tx_flow[step++] = &ux_plugin_approval_display_step;
    //     ux_approval_tx_flow[step++] = &ux_plugin_approval_after_step;
    // } else {
        // We're in a regular transaction, just show the amount and the address
        ux_approval_tx_flow[step++] = &ux_approval_amount_step;
        ux_approval_tx_flow[step++] = &ux_approval_address_step;
        ux_approval_tx_flow[step++] = &ux_approval_nonce_step;
    // }

    // if (N_storage.displayNonce) {
    //     ux_approval_tx_flow[step++] = &ux_approval_nonce_step;
    // }

    // uint64_t chain_id = get_chain_id();
    // if (chainConfig->chainId == ETHEREUM_MAINNET_CHAINID && chain_id != chainConfig->chainId) {
        ux_approval_tx_flow[step++] = &ux_approval_network_step;
    // }

    ux_approval_tx_flow[step++] = &ux_approval_fees_step;
    ux_approval_tx_flow[step++] = &ux_approval_accept_step;
    ux_approval_tx_flow[step++] = &ux_approval_reject_step;
    ux_approval_tx_flow[step++] = FLOW_END_STEP;

    ux_flow_init(0, ux_approval_tx_flow, NULL);
}