#ifndef __TASVIR_ARRAY__
#define __TASVIR_ARRAY__

#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>

#include <tasvir/tasvir.h>

namespace tasvir {

template <typename T = double>
class Array {
   public:
    typedef T value_type;
    typedef size_t size_type;
    enum OpType {

    };

    Array() = default;
    ~Array() = default;

    inline T* DataParent() { return _parent->_data; }
    inline T* DataWorker() { return _data; }

    /* Allocate and initialize the data structure in a tasvir area.
     * Blocks until all instances are up and all areas are created
     * Call once per name.
     */
    static Array<T>* Allocate(const char* parent_name, uint64_t wid, std::size_t nr_workers, std::size_t nr_elements);

    void Barrier() {
        std::size_t wid;
        _version_done = _version;
        tasvir_log(&_version_done, sizeof(_version_done));
        tasvir_service_wait(5 * S2US, true);

        if (_wid == 0) {
            for (wid = 0; wid < _nr_workers; wid++)
                while (_workers[wid]->_version_done != _parent->_version)
                    tasvir_service_wait(S2US, false);
            _parent->_version_done = _parent->_version;
            _parent->_version++;
            tasvir_log(&_parent->_version_done, sizeof(_version_done));
            tasvir_log(&_parent->_version, sizeof(_version));
        }

        /* block until parent changes version */
        while (_version_done == _parent->_version)
            tasvir_service();

        _version = _parent->_version;
        tasvir_log(&_version, sizeof(_version));
    }

    void ReduceAdd() {
        if (_wid != 0)
            return;
        tasvir_log(_parent->_data, sizeof(double) * _size);
        std::fill(_parent->_data, _parent->_data + _size, 0);
        for (std::size_t w = 0; w < _nr_workers; w++) {
            for (int i = 0; i < _size; i++) {
                _parent->_data[i] += _workers[w]->_data[i];
            }
        }
    }

    template <typename Map>
    void ReduceSelect(const Map& map) {
        for (const auto& c : map)
            std::copy(&_data[c[0]], &_data[c[1]], &_parent->_data[c[0]]);
    }

    template <typename Map>
    void CopySelect(const Map& map) {
        for (const auto& c : map)
            std::copy(&_parent->_data[c[0]], &_parent->_data[c[1]], &_data[c[0]]);
    }

   private:
    static constexpr uint64_t MS2US = 1000;
    static constexpr uint64_t S2US = 1000 * MS2US;
    size_type _size; /* the number of elements in the array */

    std::size_t _nr_workers; /* number of workers under this */
    uint64_t _wid;           /* worker identifier */
    uint64_t _version;       /* version we are processing now */
    uint64_t _version_done;  /* version we finished processing last */
    uint64_t _initialized;   /* sentinel to ensure initialization */

    alignas(64) Array<T>* _parent;   /* pointer to the parent */
    Array<T>* _workers[1024];        /* pointer to workers each with their own version of the data */
    alignas(64) value_type _data[1]; /* raw memory for data */
};

template <typename T>
Array<T>* Array<T>::Allocate(const char* parent_name, uint64_t wid, std::size_t nr_workers, std::size_t nr_elements) {
    static_assert(std::is_pod<Array<T>>::value, "Array<T> is not POD.");
    static_assert(std::is_trivial<Array<T>>::value, "Array<T> is not trivial.");
    static_assert(std::is_trivially_copyable<Array<T>>::value, "Array<T> is not trivially copyable.");

    Array<T>* parent;
    Array<T>* worker;

    tasvir_area_desc* d;
    tasvir_str name;

    tasvir_area_desc param = {};
    param.len = sizeof(T) * nr_elements + sizeof(Array<T>);
    param.sync_int_us = 50 * MS2US;
    param.sync_ext_us = 500 * MS2US;
    snprintf(param.name, sizeof(param.name), "%s-%d", parent_name, wid);
    d = tasvir_new(param);
    if (!d)
        return NULL;

    worker = reinterpret_cast<Array<T>*>(tasvir_data(d));
    tasvir_log(worker, sizeof(*worker));
    worker->_size = nr_elements;
    worker->_nr_workers = 0;
    worker->_wid = wid;
    worker->_version = 1;
    worker->_version_done = 0;

    if (wid == 0) {
        parent = reinterpret_cast<Array<T>*>(tasvir_data(d));
        for (std::size_t i = 0; i < nr_workers; i++) {
            snprintf(name, sizeof(name), "%s-%d", name, i);
            d = tasvir_attach_wait(5 * S2US, name);
            if (!d)
                return NULL;
            parent->_workers[i] = reinterpret_cast<Array<T>*>(tasvir_data(d));
            while (!parent->_workers[i] || parent->_workers[i]->_initialized != 0xdeadbeef)
                tasvir_service_wait(S2US, false);
        }
        parent->_initialized = 0xdeadbeef;
        tasvir_log(parent, sizeof(*parent));
        tasvir_service_wait(5 * S2US, true);
    } else {
        worker->_initialized = 0xdeadbeef;
        tasvir_service_wait(5 * S2US, true);
        snprintf(name, sizeof(name), "%s-0", name);
        // TODO: add option to attach to RW copy
        d = tasvir_attach_wait(5 * S2US, name);
        if (!d)
            return NULL;
        parent = reinterpret_cast<Array<T>*>(tasvir_data(d));
        while (parent->_initialized != 0xdeadbeef)
            tasvir_service_wait(S2US, false);
    }

    tasvir_log(worker, sizeof(*worker));
    worker->_parent = parent;
    worker->_nr_workers = parent->_nr_workers;
    std::copy(parent->_workers, parent->_workers + nr_workers, worker->_workers);
    worker->Barrier();

    return worker;
}

}  // namespace tasvir
#endif
