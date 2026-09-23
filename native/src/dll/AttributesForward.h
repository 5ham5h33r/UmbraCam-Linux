#pragma once
// IMFActivate derives from IMFAttributes; WRL gives us IUnknown but the 30
// attribute methods still need bodies. This macro forwards them to an inner
// store created with MFCreateAttributes.

#define G14IR_FORWARD_IMFATTRIBUTES(store)                                                         \
    IFACEMETHODIMP GetItem(REFGUID k, PROPVARIANT* v) override { return (store)->GetItem(k, v); } \
    IFACEMETHODIMP GetItemType(REFGUID k, MF_ATTRIBUTE_TYPE* t) override {                         \
        return (store)->GetItemType(k, t);                                                         \
    }                                                                                              \
    IFACEMETHODIMP CompareItem(REFGUID k, REFPROPVARIANT v, BOOL* r) override {                    \
        return (store)->CompareItem(k, v, r);                                                      \
    }                                                                                              \
    IFACEMETHODIMP Compare(IMFAttributes* a, MF_ATTRIBUTES_MATCH_TYPE t, BOOL* r) override {       \
        return (store)->Compare(a, t, r);                                                          \
    }                                                                                              \
    IFACEMETHODIMP GetUINT32(REFGUID k, UINT32* v) override { return (store)->GetUINT32(k, v); }   \
    IFACEMETHODIMP GetUINT64(REFGUID k, UINT64* v) override { return (store)->GetUINT64(k, v); }   \
    IFACEMETHODIMP GetDouble(REFGUID k, double* v) override { return (store)->GetDouble(k, v); }   \
    IFACEMETHODIMP GetGUID(REFGUID k, GUID* v) override { return (store)->GetGUID(k, v); }         \
    IFACEMETHODIMP GetStringLength(REFGUID k, UINT32* l) override {                                \
        return (store)->GetStringLength(k, l);                                                     \
    }                                                                                              \
    IFACEMETHODIMP GetString(REFGUID k, LPWSTR s, UINT32 c, UINT32* l) override {                  \
        return (store)->GetString(k, s, c, l);                                                     \
    }                                                                                              \
    IFACEMETHODIMP GetAllocatedString(REFGUID k, LPWSTR* s, UINT32* l) override {                  \
        return (store)->GetAllocatedString(k, s, l);                                               \
    }                                                                                              \
    IFACEMETHODIMP GetBlobSize(REFGUID k, UINT32* s) override {                                    \
        return (store)->GetBlobSize(k, s);                                                         \
    }                                                                                              \
    IFACEMETHODIMP GetBlob(REFGUID k, UINT8* b, UINT32 c, UINT32* s) override {                    \
        return (store)->GetBlob(k, b, c, s);                                                       \
    }                                                                                              \
    IFACEMETHODIMP GetAllocatedBlob(REFGUID k, UINT8** b, UINT32* s) override {                    \
        return (store)->GetAllocatedBlob(k, b, s);                                                 \
    }                                                                                              \
    IFACEMETHODIMP GetUnknown(REFGUID k, REFIID i, LPVOID* v) override {                           \
        return (store)->GetUnknown(k, i, v);                                                       \
    }                                                                                              \
    IFACEMETHODIMP SetItem(REFGUID k, REFPROPVARIANT v) override {                                 \
        return (store)->SetItem(k, v);                                                             \
    }                                                                                              \
    IFACEMETHODIMP DeleteItem(REFGUID k) override { return (store)->DeleteItem(k); }               \
    IFACEMETHODIMP DeleteAllItems() override { return (store)->DeleteAllItems(); }                 \
    IFACEMETHODIMP SetUINT32(REFGUID k, UINT32 v) override { return (store)->SetUINT32(k, v); }    \
    IFACEMETHODIMP SetUINT64(REFGUID k, UINT64 v) override { return (store)->SetUINT64(k, v); }    \
    IFACEMETHODIMP SetDouble(REFGUID k, double v) override { return (store)->SetDouble(k, v); }    \
    IFACEMETHODIMP SetGUID(REFGUID k, REFGUID v) override { return (store)->SetGUID(k, v); }       \
    IFACEMETHODIMP SetString(REFGUID k, LPCWSTR v) override { return (store)->SetString(k, v); }   \
    IFACEMETHODIMP SetBlob(REFGUID k, const UINT8* b, UINT32 c) override {                         \
        return (store)->SetBlob(k, b, c);                                                          \
    }                                                                                              \
    IFACEMETHODIMP SetUnknown(REFGUID k, IUnknown* u) override {                                   \
        return (store)->SetUnknown(k, u);                                                          \
    }                                                                                              \
    IFACEMETHODIMP LockStore() override { return (store)->LockStore(); }                           \
    IFACEMETHODIMP UnlockStore() override { return (store)->UnlockStore(); }                       \
    IFACEMETHODIMP GetCount(UINT32* c) override { return (store)->GetCount(c); }                   \
    IFACEMETHODIMP GetItemByIndex(UINT32 i, GUID* k, PROPVARIANT* v) override {                    \
        return (store)->GetItemByIndex(i, k, v);                                                   \
    }                                                                                              \
    IFACEMETHODIMP CopyAllItems(IMFAttributes* d) override { return (store)->CopyAllItems(d); }

