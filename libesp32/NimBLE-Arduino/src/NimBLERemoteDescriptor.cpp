/*
 * NimBLERemoteDescriptor.cpp
 *
 *  Created: on Jan 27 2020
 *      Author H2zero
 *
 * Originally:
 *
 * BLERemoteDescriptor.cpp
 *
 *  Created on: Jul 8, 2017
 *      Author: kolban
 */
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include "nimconfig.h"
#if defined( CONFIG_BT_NIMBLE_ROLE_CENTRAL)

#include "NimBLERemoteDescriptor.h"
#include "NimBLEUtils.h"
#include "NimBLELog.h"

static const char* LOG_TAG = "NimBLERemoteDescriptor";

/**
 * @brief Remote descriptor constructor.
 * @param [in] Reference to the Characteristic that this belongs to.
 * @param [in] Reference to the struct that contains the descriptor information.
 */
NimBLERemoteDescriptor::NimBLERemoteDescriptor(NimBLERemoteCharacteristic* pRemoteCharacteristic,
                                               const struct ble_gatt_dsc *dsc)
{
    switch (dsc->uuid.u.type) {
        case BLE_UUID_TYPE_16:
            m_uuid = NimBLEUUID(dsc->uuid.u16.value);
            break;
        case BLE_UUID_TYPE_32:
            m_uuid = NimBLEUUID(dsc->uuid.u32.value);
            break;
        case BLE_UUID_TYPE_128:
            m_uuid = NimBLEUUID(const_cast<ble_uuid128_t*>(&dsc->uuid.u128));
            break;
        default:
            m_uuid = nullptr;
            break;
    }
    m_handle                = dsc->handle;
    m_pRemoteCharacteristic = pRemoteCharacteristic;

}


/**
 * @brief Retrieve the handle associated with this remote descriptor.
 * @return The handle associated with this remote descriptor.
 */
uint16_t NimBLERemoteDescriptor::getHandle() {
    return m_handle;
} // getHandle


/**
 * @brief Get the characteristic that owns this descriptor.
 * @return The characteristic that owns this descriptor.
 */
NimBLERemoteCharacteristic* NimBLERemoteDescriptor::getRemoteCharacteristic() {
    return m_pRemoteCharacteristic;
} // getRemoteCharacteristic


/**
 * @brief Retrieve the UUID associated this remote descriptor.
 * @return The UUID associated this remote descriptor.
 */
NimBLEUUID NimBLERemoteDescriptor::getUUID() {
    return m_uuid;
} // getUUID


/**
 * @brief Callback for Descriptor read operation.
 * @return 0 or error code.
 */
int NimBLERemoteDescriptor::onReadCB(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr, void *arg)
{
    NimBLERemoteDescriptor* desc = (NimBLERemoteDescriptor*)arg;
    uint16_t conn_id = desc->getRemoteCharacteristic()->getRemoteService()->getClient()->getConnId();

        // Make sure the discovery is for this device
    if(conn_id != conn_handle){
        return 0;
    }

    NIMBLE_LOGD(LOG_TAG, "Read complete; status=%d conn_handle=%d", error->status, conn_handle);

    if(error->status == 0){
        if(attr){
            NIMBLE_LOGD(LOG_TAG, "Got %d bytes", attr->om->om_len);

            desc->m_value += std::string((char*) attr->om->om_data, attr->om->om_len);
            return 0;
        }
    }

    // Read complete release semaphore and let the app can continue.
    desc->m_semaphoreReadDescrEvt.give(error->status);
    return 0;
}


std::string NimBLERemoteDescriptor::readValue() {
    NIMBLE_LOGD(LOG_TAG, ">> Descriptor readValue: %s", toString().c_str());

    int rc = 0;
    int retryCount = 1;
    // Clear the value before reading.
    m_value = "";

    NimBLEClient* pClient = getRemoteCharacteristic()->getRemoteService()->getClient();

    // Check to see that we are connected.
    if (!pClient->isConnected()) {
        NIMBLE_LOGE(LOG_TAG, "Disconnected");
        return "";
    }

    do {
        m_semaphoreReadDescrEvt.take("ReadDescriptor");

        rc = ble_gattc_read_long(pClient->getConnId(), m_handle, 0,
                                 NimBLERemoteDescriptor::onReadCB,
                                 this);
        if (rc != 0) {
            NIMBLE_LOGE(LOG_TAG, "Error: Failed to read descriptor; rc=%d, %s",
                                  rc, NimBLEUtils::returnCodeToString(rc));
            m_semaphoreReadDescrEvt.give(0);
            return "";
        }

        rc = m_semaphoreReadDescrEvt.wait("ReadDescriptor");

        switch(rc){
            case 0:
            case BLE_HS_EDONE:
                rc = 0;
                break;
            // Descriptor is not long-readable, return with what we have.
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG):
                NIMBLE_LOGI(LOG_TAG, "Attribute not long");
                rc = 0;
                break;
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN):
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR):
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC):
                if (retryCount && pClient->secureConnection())
                    break;
            /* Else falls through. */
            default:
                return "";
        }
    } while(rc != 0 && retryCount--);

    NIMBLE_LOGD(LOG_TAG, "<< Descriptor readValue(): length: %d", m_value.length());
    return m_value;
} // readValue


uint8_t NimBLERemoteDescriptor::readUInt8() {
    std::string value = readValue();
    if (value.length() >= 1) {
        return (uint8_t) value[0];
    }
    return 0;
} // readUInt8


uint16_t NimBLERemoteDescriptor::readUInt16() {
    std::string value = readValue();
    if (value.length() >= 2) {
        return *(uint16_t*) value.data();
    }
    return 0;
} // readUInt16


uint32_t NimBLERemoteDescriptor::readUInt32() {
    std::string value = readValue();
    if (value.length() >= 4) {
        return *(uint32_t*) value.data();
    }
    return 0;
} // readUInt32


/**
 * @brief Return a string representation of this BLE Remote Descriptor.
 * @retun A string representation of this BLE Remote Descriptor.
 */
std::string NimBLERemoteDescriptor::toString() {
    std::string res = "Descriptor: uuid: " + getUUID().toString();
    char val[6];
    res += ", handle: ";
    snprintf(val, sizeof(val), "%d", getHandle());
    res += val;

    return res;
} // toString


/**
 * @brief Callback for descriptor write operation.
 * @return 0 or error code.
 */
int NimBLERemoteDescriptor::onWriteCB(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr, void *arg)
{
    NimBLERemoteDescriptor* descriptor = (NimBLERemoteDescriptor*)arg;

    // Make sure the discovery is for this device
    if(descriptor->getRemoteCharacteristic()->getRemoteService()->getClient()->getConnId() != conn_handle){
        return 0;
    }

    NIMBLE_LOGD(LOG_TAG, "Write complete; status=%d conn_handle=%d", error->status, conn_handle);

    descriptor->m_semaphoreDescWrite.give(error->status);

    return 0;
}


/**
 * @brief Write data to the BLE Remote Descriptor.
 * @param [in] data The data to send to the remote descriptor.
 * @param [in] length The length of the data to send.
 * @param [in] response True if we expect a response.
 */
bool NimBLERemoteDescriptor::writeValue(const uint8_t* data, size_t length, bool response) {

    NIMBLE_LOGD(LOG_TAG, ">> Descriptor writeValue: %s", toString().c_str());

    NimBLEClient* pClient = getRemoteCharacteristic()->getRemoteService()->getClient();

    int rc = 0;
    int retryCount = 1;
    uint16_t mtu;

    // Check to see that we are connected.
    if (!pClient->isConnected()) {
        NIMBLE_LOGE(LOG_TAG, "Disconnected");
        return false;
    }

    mtu = ble_att_mtu(pClient->getConnId()) - 3;

    // Check if the data length is longer than we can write in 1 connection event.
    // If so we must do a long write which requires a response.
    if(length <= mtu && !response) {
        rc =  ble_gattc_write_no_rsp_flat(pClient->getConnId(), m_handle, data, length);
        return (rc == 0);
    }

    do {
        m_semaphoreDescWrite.take("WriteDescriptor");

        if(length > mtu) {
            NIMBLE_LOGI(LOG_TAG,"long write %d bytes", length);
            os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
            rc = ble_gattc_write_long(pClient->getConnId(), m_handle, 0, om,
                                      NimBLERemoteDescriptor::onWriteCB,
                                      this);
        } else {
            rc = ble_gattc_write_flat(pClient->getConnId(), m_handle,
                                      data, length,
                                      NimBLERemoteDescriptor::onWriteCB,
                                      this);
        }

        if (rc != 0) {
            NIMBLE_LOGE(LOG_TAG, "Error: Failed to write descriptor; rc=%d", rc);
            m_semaphoreDescWrite.give();
            return false;
        }

        rc = m_semaphoreDescWrite.wait("WriteDescriptor");

        switch(rc){
            case 0:
            case BLE_HS_EDONE:
                rc = 0;
                break;
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_LONG):
                NIMBLE_LOGE(LOG_TAG, "Long write not supported by peer; Truncating length to %d", mtu);
                retryCount++;
                length = mtu;
                break;

            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN):
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR):
            case BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC):
                if (retryCount && pClient->secureConnection())
                    break;
            /* Else falls through. */
            default:
                return false;
        }
    } while(rc != 0 && retryCount--);

    NIMBLE_LOGD(LOG_TAG, "<< Descriptor writeValue, rc: %d",rc);
    return (rc == 0);
} // writeValue


/**
 * @brief Write data represented as a string to the BLE Remote Descriptor.
 * @param [in] newValue The data to send to the remote descriptor.
 * @param [in] response True if we expect a response.
 */
bool NimBLERemoteDescriptor::writeValue(const std::string &newValue, bool response) {
    return writeValue((uint8_t*) newValue.data(), newValue.length(), response);
} // writeValue


/**
 * @brief Write a byte value to the Descriptor.
 * @param [in] The single byte to write.
 * @param [in] True if we expect a response.
 */
bool NimBLERemoteDescriptor::writeValue(uint8_t newValue, bool response) {
    return writeValue(&newValue, 1, response);
} // writeValue


/**
 * @brief In the event of an error this is called to make sure we don't block.
 */
void NimBLERemoteDescriptor::releaseSemaphores() {
    m_semaphoreDescWrite.give(1);
    m_semaphoreReadDescrEvt.give(1);
}

#endif // #if defined( CONFIG_BT_NIMBLE_ROLE_CENTRAL)
#endif /* CONFIG_BT_ENABLED */
