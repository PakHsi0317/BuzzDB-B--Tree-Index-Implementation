#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <map>
#include <vector>
#include <list>
#include <unordered_map>
#include <string>
#include <memory>
#include <sstream>
#include <limits>
#include <thread>
#include <queue>
#include <optional>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <cassert>
#include <cstring> 
#include <exception>
#include <atomic>
#include <set>

#define UNUSED(p)  ((void)(p))

#define ASSERT_WITH_MESSAGE(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion \033[1;31mFAILED\033[0m: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while(0)

enum FieldType { INT, FLOAT, STRING };

// Define a basic Field variant class that can hold different types
class Field {
public:
    FieldType type;
    std::unique_ptr<char[]> data;
    size_t data_length;

public:
    Field(int i) : type(INT) { 
        data_length = sizeof(int);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &i, data_length);
    }

    Field(float f) : type(FLOAT) { 
        data_length = sizeof(float);
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), &f, data_length);
    }

    Field(const std::string& s) : type(STRING) {
        data_length = s.size() + 1;  // include null-terminator
        data = std::make_unique<char[]>(data_length);
        std::memcpy(data.get(), s.c_str(), data_length);
    }

    Field& operator=(const Field& other) {
        if (&other == this) {
            return *this;
        }
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
        return *this;
    }

    Field(Field&& other){
        type = other.type;
        data_length = other.data_length;
        std::memcpy(data.get(), other.data.get(), data_length);
    }

    FieldType getType() const { return type; }
    int asInt() const { 
        return *reinterpret_cast<int*>(data.get());
    }
    float asFloat() const { 
        return *reinterpret_cast<float*>(data.get());
    }
    std::string asString() const { 
        return std::string(data.get());
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << type << ' ' << data_length << ' ';
        if (type == STRING) {
            buffer << data.get() << ' ';
        } else if (type == INT) {
            buffer << *reinterpret_cast<int*>(data.get()) << ' ';
        } else if (type == FLOAT) {
            buffer << *reinterpret_cast<float*>(data.get()) << ' ';
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Field> deserialize(std::istream& in) {
        int type; in >> type;
        size_t length; in >> length;
        if (type == STRING) {
            std::string val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == INT) {
            int val; in >> val;
            return std::make_unique<Field>(val);
        } else if (type == FLOAT) {
            float val; in >> val;
            return std::make_unique<Field>(val);
        }
        return nullptr;
    }

    void print() const{
        switch(getType()){
            case INT: std::cout << asInt(); break;
            case FLOAT: std::cout << asFloat(); break;
            case STRING: std::cout << asString(); break;
        }
    }
};

class Tuple {
public:
    std::vector<std::unique_ptr<Field>> fields;

    void addField(std::unique_ptr<Field> field) {
        fields.push_back(std::move(field));
    }

    size_t getSize() const {
        size_t size = 0;
        for (const auto& field : fields) {
            size += field->data_length;
        }
        return size;
    }

    std::string serialize() {
        std::stringstream buffer;
        buffer << fields.size() << ' ';
        for (const auto& field : fields) {
            buffer << field->serialize();
        }
        return buffer.str();
    }

    void serialize(std::ofstream& out) {
        std::string serializedData = this->serialize();
        out << serializedData;
    }

    static std::unique_ptr<Tuple> deserialize(std::istream& in) {
        auto tuple = std::make_unique<Tuple>();
        size_t fieldCount; in >> fieldCount;
        for (size_t i = 0; i < fieldCount; ++i) {
            tuple->addField(Field::deserialize(in));
        }
        return tuple;
    }

    void print() const {
        for (const auto& field : fields) {
            field->print();
            std::cout << " ";
        }
        std::cout << "\n";
    }
};

static constexpr size_t PAGE_SIZE = 4096;  // Fixed page size
static constexpr size_t MAX_SLOTS = 512;   // Fixed number of slots
static constexpr size_t MAX_PAGES= 1000;   // Total Number of pages that can be stored
uint16_t INVALID_VALUE = std::numeric_limits<uint16_t>::max(); // Sentinel value

struct Slot {
    bool empty = true;                 // Is the slot empty?    
    uint16_t offset = INVALID_VALUE;    // Offset of the slot within the page
    uint16_t length = INVALID_VALUE;    // Length of the slot
};

// Slotted Page class
class SlottedPage {
public:
    std::unique_ptr<char[]> page_data = std::make_unique<char[]>(PAGE_SIZE);
    size_t metadata_size = sizeof(Slot) * MAX_SLOTS;

    SlottedPage(){
        // Empty page -> initialize slot array inside page
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            slot_array[slot_itr].length = INVALID_VALUE;
        }
    }

    // Add a tuple, returns true if it fits, false otherwise.
    bool addTuple(std::unique_ptr<Tuple> tuple) {

        // Serialize the tuple into a char array
        auto serializedTuple = tuple->serialize();
        size_t tuple_size = serializedTuple.size();

        //std::cout << "Tuple size: " << tuple_size << " bytes\n";
        assert(tuple_size == 38);

        // Check for first slot with enough space
        size_t slot_itr = 0;
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());        
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == true and 
                slot_array[slot_itr].length >= tuple_size) {
                break;
            }
        }
        if (slot_itr == MAX_SLOTS){
            //std::cout << "Page does not contain an empty slot with sufficient space to store the tuple.";
            return false;
        }

        // Identify the offset where the tuple will be placed in the page
        // Update slot meta-data if needed
        slot_array[slot_itr].empty = false;
        size_t offset = INVALID_VALUE;
        if (slot_array[slot_itr].offset == INVALID_VALUE){
            if(slot_itr != 0){
                auto prev_slot_offset = slot_array[slot_itr - 1].offset;
                auto prev_slot_length = slot_array[slot_itr - 1].length;
                offset = prev_slot_offset + prev_slot_length;
            }
            else{
                offset = metadata_size;
            }

            slot_array[slot_itr].offset = offset;
        }
        else{
            offset = slot_array[slot_itr].offset;
        }

        if(offset + tuple_size >= PAGE_SIZE){
            slot_array[slot_itr].empty = true;
            slot_array[slot_itr].offset = INVALID_VALUE;
            return false;
        }

        assert(offset != INVALID_VALUE);
        assert(offset >= metadata_size);
        assert(offset + tuple_size < PAGE_SIZE);

        if (slot_array[slot_itr].length == INVALID_VALUE){
            slot_array[slot_itr].length = tuple_size;
        }

        // Copy serialized data into the page
        std::memcpy(page_data.get() + offset, 
                    serializedTuple.c_str(), 
                    tuple_size);

        return true;
    }

    void deleteTuple(size_t index) {
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        size_t slot_itr = 0;
        for (; slot_itr < MAX_SLOTS; slot_itr++) {
            if(slot_itr == index and
               slot_array[slot_itr].empty == false){
                slot_array[slot_itr].empty = true;
                break;
               }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void print() const{
        Slot* slot_array = reinterpret_cast<Slot*>(page_data.get());
        for (size_t slot_itr = 0; slot_itr < MAX_SLOTS; slot_itr++) {
            if (slot_array[slot_itr].empty == false){
                assert(slot_array[slot_itr].offset != INVALID_VALUE);
                const char* tuple_data = page_data.get() + slot_array[slot_itr].offset;
                std::istringstream iss(tuple_data);
                auto loadedTuple = Tuple::deserialize(iss);
                std::cout << "Slot " << slot_itr << " : [";
                std::cout << (uint16_t)(slot_array[slot_itr].offset) << "] :: ";
                loadedTuple->print();
            }
        }
        std::cout << "\n";
    }
};

const std::string database_filename = "buzzdb.dat";

class StorageManager {
public:    
    std::fstream fileStream;
    size_t num_pages = 0;
    std::mutex io_mutex;

public:
    StorageManager(bool truncate_mode = true){
        auto flags =  truncate_mode ? std::ios::in | std::ios::out | std::ios::trunc 
            : std::ios::in | std::ios::out;
        fileStream.open(database_filename, flags);
        if (!fileStream) {
            // If file does not exist, create it
            fileStream.clear(); // Reset the state
            fileStream.open(database_filename, truncate_mode ? (std::ios::out | std::ios::trunc) : std::ios::out);
        }
        fileStream.close(); 
        fileStream.open(database_filename, std::ios::in | std::ios::out); 

        fileStream.seekg(0, std::ios::end);
        num_pages = fileStream.tellg() / PAGE_SIZE;

        if(num_pages == 0){
            extend();
        }

    }

    ~StorageManager() {
        if (fileStream.is_open()) {
            fileStream.close();
        }
    }

    // Read a page from disk
    std::unique_ptr<SlottedPage> load(uint16_t page_id) {
        fileStream.seekg(page_id * PAGE_SIZE, std::ios::beg);
        auto page = std::make_unique<SlottedPage>();
        // Read the content of the file into the page
        if(fileStream.read(page->page_data.get(), PAGE_SIZE)){
            //std::cout << "Page read successfully from file." << std::endl;
        }
        else{
            std::cerr << "Error: Unable to read data from the file. \n";
            exit(-1);
        }
        return page;
    }

    // Write a page to disk
    void flush(uint16_t page_id, const SlottedPage& page) {
        size_t page_offset = page_id * PAGE_SIZE;        

        // Move the write pointer
        fileStream.seekp(page_offset, std::ios::beg);
        fileStream.write(page.page_data.get(), PAGE_SIZE);        
        fileStream.flush();
    }

    // Extend database file by one page
    void extend() {
        // Create a slotted page
        auto empty_slotted_page = std::make_unique<SlottedPage>();

        // Move the write pointer
        fileStream.seekp(0, std::ios::end);

        // Write the page to the file, extending it
        fileStream.write(empty_slotted_page->page_data.get(), PAGE_SIZE);
        fileStream.flush();

        // Update number of pages
        num_pages += 1;
    }

    void extend(uint64_t till_page_id) {
        std::lock_guard<std::mutex>  io_guard(io_mutex); 
        uint64_t write_size = std::max(static_cast<uint64_t>(0), till_page_id + 1 - num_pages) * PAGE_SIZE;
        if(write_size > 0 ) {
            // std::cout << "Extending database file till page id : "<<till_page_id<<" \n";
            char* buffer = new char[write_size];
            std::memset(buffer, 0, write_size);

            fileStream.seekp(0, std::ios::end);
            fileStream.write(buffer, write_size);
            fileStream.flush();
            
            num_pages = till_page_id+1;
        }
    }

};

using PageID = uint16_t;

class Policy {
public:
    virtual bool touch(PageID page_id) = 0;
    virtual PageID evict() = 0;
    virtual ~Policy() = default;
};

void printList(std::string list_name, const std::list<PageID>& myList) {
        std::cout << list_name << " :: ";
        for (const PageID& value : myList) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
}

class LruPolicy : public Policy {
private:
    // List to keep track of the order of use
    std::list<PageID> lruList;

    // Map to find a page's iterator in the list efficiently
    std::unordered_map<PageID, std::list<PageID>::iterator> map;

    size_t cacheSize;

public:

    LruPolicy(size_t cacheSize) : cacheSize(cacheSize) {}

    bool touch(PageID page_id) override {
        //printList("LRU", lruList);

        bool found = false;
        // If page already in the list, remove it
        if (map.find(page_id) != map.end()) {
            found = true;
            lruList.erase(map[page_id]);
            map.erase(page_id);            
        }

        // If cache is full, evict
        if(lruList.size() == cacheSize){
            evict();
        }

        if(lruList.size() < cacheSize){
            // Add the page to the front of the list
            lruList.emplace_front(page_id);
            map[page_id] = lruList.begin();
        }

        return found;
    }

    PageID evict() override {
        // Evict the least recently used page
        PageID evictedPageId = INVALID_VALUE;
        if(lruList.size() != 0){
            evictedPageId = lruList.back();
            map.erase(evictedPageId);
            lruList.pop_back();
        }
        return evictedPageId;
    }

};

constexpr size_t MAX_PAGES_IN_MEMORY = 10;

class BufferManager {
private:
    using PageMap = std::unordered_map<PageID, SlottedPage>;

    StorageManager storage_manager;
    PageMap pageMap;
    std::unique_ptr<Policy> policy;

public:
    BufferManager(bool storage_manager_truncate_mode = true): 
        storage_manager(storage_manager_truncate_mode),
        policy(std::make_unique<LruPolicy>(MAX_PAGES_IN_MEMORY)) {
            storage_manager.extend(MAX_PAGES);
    }
    
    ~BufferManager() {
        for (auto& pair : pageMap) {
            flushPage(pair.first);
        }
    }

    SlottedPage& fix_page(int page_id) {
        auto it = pageMap.find(page_id);
        if (it != pageMap.end()) {
            policy->touch(page_id);
            return pageMap.find(page_id)->second;
        }

        if (pageMap.size() >= MAX_PAGES_IN_MEMORY) {
            auto evictedPageId = policy->evict();
            if(evictedPageId != INVALID_VALUE){
                // std::cout << "Evicting page " << evictedPageId << "\n";
                storage_manager.flush(evictedPageId, 
                                      pageMap[evictedPageId]);
            }
        }

        auto page = storage_manager.load(page_id);
        policy->touch(page_id);
        // std::cout << "Loading page: " << page_id << "\n";
        pageMap[page_id] = std::move(*page);
        return pageMap[page_id];
    }

    void flushPage(int page_id) {
        storage_manager.flush(page_id, pageMap[page_id]);
    }

    void extend(){
        storage_manager.extend();
    }
    
    size_t getNumPages(){
        return storage_manager.num_pages;
    }

};

template<typename KeyT, typename ValueT, typename ComparatorT, size_t PageSize>
class BTree {
    public:
        struct Node {
            /// ID of this node.
            uint64_t node_id = INVALID_VALUE;

            /// ID of the parent node. INVALID for the root.
            /// Ensure parent pointers are maintained correctly across node splits.
            uint64_t parent_node_id = INVALID_VALUE;

            /// The level in the tree.
            uint16_t level;

            /// The number of children.
            uint16_t count;

            /// TODO: Add additional members as needed

            // Constructor
            Node(uint16_t level, uint16_t count)
                : level(level), count(count) {}

            /// Is the node a leaf node?
            bool is_leaf() const { return level == 0; }
        };

        struct InnerNode: public Node {
            /// The capacity of a node.
            /// TODO think about the capacity that the nodes have.
            static constexpr uint32_t kCapacity = 42;

            /// The keys.
            KeyT keys[kCapacity - 1];

            /// The children.
            uint64_t children[kCapacity];

            /// Constructor.
            //change 0 to 1
            InnerNode() : Node(1, 0) {}

            /// Get the index of the child node which could have the provided key.
            /// @param[in] key          The key that should be searched.
            size_t find_child_index(const KeyT &key) {
                // TODO: Implement this function and remove UNUSED(...) calls.
                // UNUSED(key);
                // return 0;
                // keys[0..count-2] are separators between children[0..count-1]
                size_t child_count = static_cast<size_t>(this->count);
                if (child_count <= 1) return 0;
                size_t sep_count = child_count - 1;
                for (size_t i = 0; i < sep_count; ++i) {
                    if (key < keys[i]) return i;
                }
                return sep_count; // rightmost
            }
            void insert_at(size_t index, const KeyT &key, uint64_t child_id) {
                // this->count == number of children currently present
                size_t child_count = static_cast<size_t>(this->count);
                size_t sep_count   = (child_count == 0) ? 0 : (child_count - 1);

                // Shift children to open a slot at index+1
                for (size_t i = child_count; i > index + 1; --i) {
                    children[i] = children[i - 1];
                }
                // Shift separators to open a slot at index
                for (size_t i = sep_count; i > index; --i) {
                    keys[i] = keys[i - 1];
                }

                // Place new separator and right child
                keys[index] = key;
                children[index+1] = child_id;

                this->count = static_cast<uint16_t>(child_count + 1);
            }
            /// Insert a key.
            /// @param[in] key          The separator that should be inserted.
            /// @param[in] child_id     The id of the child page that should be inserted.
            void insert(const KeyT &key, uint64_t child_id) {
                // TODO: Implement this function and remove UNUSED(...) calls.
                // UNUSED(key);
                // UNUSED(child_id);
                size_t child_count = static_cast<size_t>(this->count);
                size_t sep_count = (child_count == 0) ? 0 : (child_count - 1);
                size_t pos = 0;
                while (pos < sep_count && keys[pos] < key) ++pos;

                // Shift children to make room at pos+1
                for (size_t i = child_count; i > pos + 1; --i) {
                    children[i] = children[i - 1];
                }
                // Shift keys to make room at pos
                for (size_t i = sep_count; i > pos; --i) {
                    keys[i] = keys[i - 1];
                }
                keys[pos] = key;
                children[pos + 1] = child_id;
                this->count = static_cast<uint16_t>(child_count + 1);
            }
        };

        struct LeafNode: public Node {
            /// The capacity of a node.
            /// TODO think about the capacity that the nodes have.
            static constexpr uint32_t kCapacity = 42;

            /// The keys.
            KeyT keys[kCapacity];

            /// The values.
            ValueT values[kCapacity];

            /// ID of the next LeafNode.
            uint64_t next = INVALID_VALUE;

            /// Constructor.
            LeafNode() : Node(0, 0) {}

            /// Insert a key.
            /// @param[in] key          The key that should be inserted.
            /// @param[in] value        The value that should be inserted.
            void insert(const KeyT &key, const ValueT &value) {
                // TODO: Implement this function and remove UNUSED(...) calls.
                // UNUSED(key);
                // UNUSED(value);
                size_t n = this->count;
                size_t pos = 0;
                while (pos < n && keys[pos] < key) ++pos;
                if (pos < n && !(key < keys[pos]) && !(keys[pos] < key)) {
                    values[pos] = value; // update
                    return;
                }
                for (size_t i = n; i > pos; --i) {
                    keys[i] = keys[i - 1];
                    values[i] = values[i - 1];
                }
                keys[pos] = key;
                values[pos] = value;
                this->count = static_cast<uint16_t>(n + 1);
            }

            /// Erase a key.
            void erase(const KeyT &key) {
                // TODO: Implement this function and remove UNUSED(...) calls.
                //UNUSED(key);
                if (this->count == 0) return;
                auto it = std::lower_bound(keys, keys + this->count, key, ComparatorT{});
                size_t pos = static_cast<size_t>(it - keys);
                if (pos < this->count &&
                    !ComparatorT{}(key, keys[pos]) && !ComparatorT{}(keys[pos], key)) {
                    for (size_t i = pos + 1; i < this->count; ++i) {
                        keys[i - 1] = keys[i];
                        values[i - 1] = values[i];
                    }
                    this->count--;
                }
            }
        };

        /// The root.
        std::optional<uint64_t> root;

        /// The buffer manager
        BufferManager& buffer_manager;

        /// Next page id.
        /// You don't need to worry about the page allocation.
        /// (Neither fragmentation, nor persisting free-space bitmaps)
        /// Just increment the next_page_id whenever you need a new page.
        /// This page_id could also be used to assign node_ids.
        uint64_t next_page_id;

        /// Constructor.
        BTree(BufferManager &buffer_manager): buffer_manager(buffer_manager) {
            // TODO
            root = std::nullopt;
            // (Hint: For Test 12, your reconstruction logic should trace parent relationships upward consistently.)
            next_page_id = 1;
            // Scan pages to reconstruct the root after reopening
            uint64_t num_pages = static_cast<uint64_t>(buffer_manager.getNumPages());
            uint64_t max_id = 0;
            std::optional<uint64_t> found_root;

            for (uint64_t pid = 1; pid < num_pages; ++pid) {
                SlottedPage &page = buffer_manager.fix_page(pid);
                auto *node = reinterpret_cast<Node*>(page.page_data.get());
                if (node->node_id != pid) {
                    continue;
                }
                if (node->count == 0) {
                    continue;
                }

                if (node->parent_node_id == INVALID_VALUE) {
                    if (!found_root.has_value()) {
                        found_root = pid;
                    }
                }
                if (node->node_id > max_id) max_id = node->node_id;
            }

            if (found_root.has_value()) {
                root = *found_root;
                next_page_id = max_id + 1;
            }
        }

        /// Lookup an entry in the tree.
        /// @param[in] key      The key that should be searched.
        std::optional<ValueT> lookup(const KeyT &key) {
            // TODO
            // UNUSED(key);
            // return std::optional<ValueT>();
            // Empty tree
            if (!root.has_value()) return std::optional<ValueT>();
            uint64_t pid = *root;
            while (true) {
                SlottedPage &page = buffer_manager.fix_page(pid);
                auto *node = reinterpret_cast<Node*>(page.page_data.get());
                if (node->is_leaf()) {
                    auto *leaf = reinterpret_cast<LeafNode*>(node);
                    for (uint16_t i = 0; i < leaf->count; ++i) {
                        if (!(key < leaf->keys[i]) && !(leaf->keys[i] < key))
                            return std::optional<ValueT>(leaf->values[i]);
                    }
                    return std::optional<ValueT>();
                } else {
                    auto *inner = reinterpret_cast<InnerNode*>(node);
                    size_t idx = inner->find_child_index(key);
                    pid = inner->children[idx];
                }
            }
            // Load root
            SlottedPage &page = buffer_manager.fix_page(*root);
            auto *node = reinterpret_cast<Node*>(page.page_data.get());

            // Single-leaf tree: search within leaf
            if (node->is_leaf()) {
                auto *leaf = reinterpret_cast<LeafNode*>(node);
                for (uint16_t i = 0; i < leaf->count; ++i) {
                    if (!(key < leaf->keys[i]) && !(leaf->keys[i] < key))
                        return std::optional<ValueT>(leaf->values[i]);
                }
                return std::optional<ValueT>();
            }

            // Minimal one-level inner traversal (root is inner, children are leaves)
            auto *inner = reinterpret_cast<InnerNode*>(node);

            size_t child_count = static_cast<size_t>(inner->count);
            if (child_count == 0) {
                return std::optional<ValueT>();
            }

            size_t child_index = 0;
            if (child_count == 1) {
                child_index = 0;
            } else {
                // keys[0 .. child_count-2] are separators
                size_t sep_count = child_count - 1;

                size_t idx = 0;
                while (idx < sep_count && !(key < inner->keys[idx])) {
                    ++idx;
                }

                // If we stopped at a separator greater than key, take child idx,
                // otherwise take the rightmost child.
                child_index = (idx < sep_count && (key < inner->keys[idx])) ? idx : (child_count - 1);
            }

            uint64_t child_pid = inner->children[child_index];
            SlottedPage &cpage = buffer_manager.fix_page(child_pid);
            auto *cnode = reinterpret_cast<Node*>(cpage.page_data.get());
            if (!cnode->is_leaf()) {
                // Only handling one level (root->leaf) for now
                return std::optional<ValueT>();
            }
            auto *cleaf = reinterpret_cast<LeafNode*>(cnode);
            for (uint16_t i = 0; i < cleaf->count; ++i) {
                if (!(key < cleaf->keys[i]) && !(cleaf->keys[i] < key)) {
                    return std::optional<ValueT>(cleaf->values[i]);
                }
            }
            return std::optional<ValueT>();
        }

        /// Returns the range of values between low and high, both inclusive
        /// @param[in] low The low key from which range starts
        /// @param[in] high The high key where the range ends
        std::vector<std::pair<KeyT, ValueT>> rangeQuery(const KeyT& low, const KeyT& high) {
                // TODO
                // UNUSED(low);
                // UNUSED(high);
                // return std::vector<std::pair<KeyT, ValueT>>();
                std::vector<std::pair<KeyT, ValueT>> result;
                if (!root.has_value()) return result;
                if (ComparatorT{}(high, low)) return result;

                // Descend to first leaf that could contain 'low'
                uint64_t pid = *root;
                while (true) {
                    SlottedPage &page = buffer_manager.fix_page(pid);
                    auto *node = reinterpret_cast<Node*>(page.page_data.get());
                    if (node->is_leaf()) break;
                    auto *inner = reinterpret_cast<InnerNode*>(node);
                    size_t idx = inner->find_child_index(low);
                    pid = inner->children[idx];
                }

                // Walk leaves sequentially via 'next' pointer
                while (pid != INVALID_VALUE) {
                    SlottedPage &page = buffer_manager.fix_page(pid);
                    auto *leaf = reinterpret_cast<LeafNode*>(page.page_data.get());
                    for (uint16_t i = 0; i < leaf->count; ++i) {
                        const KeyT& k = leaf->keys[i];
                        if (ComparatorT{}(k, low)) continue;
                        if (ComparatorT{}(high, k)) return result;
                        result.emplace_back(k, leaf->values[i]);
                    }
                    pid = leaf->next;
                }
                return result;
        }

        /// Erase an entry in the tree.
        /// @param[in] key      The key that should be searched.
        void erase(const KeyT &key) {
            // TODO
            //UNUSED(key);
            if (!root.has_value()) return;

            uint64_t pid = *root;
            while (true) {
                SlottedPage &page = buffer_manager.fix_page(pid);
                auto *node = reinterpret_cast<Node*>(page.page_data.get());

                if (node->is_leaf()) {
                    auto *leaf = reinterpret_cast<LeafNode*>(node);
                    leaf->erase(key);
                    buffer_manager.flushPage(pid);
                    return;
                } else {
                    auto *inner = reinterpret_cast<InnerNode*>(node);
                    size_t idx = inner->find_child_index(key);
                    pid = inner->children[idx];
                }
            }
        }

        /// Inserts a new entry into the tree.
        /// @param[in] key      The key that should be inserted.
        /// @param[in] value    The value that should be inserted.
        void insert(const KeyT &key, const ValueT &value) {
            // TODO
            // UNUSED(key);
            // UNUSED(value);
            // Empty tree → create root leaf
            if (!root.has_value()) {
                uint64_t pid = next_page_id++;
                root = pid;
                SlottedPage &page = buffer_manager.fix_page(pid);
                auto *leaf = reinterpret_cast<LeafNode*>(page.page_data.get());
                *leaf = LeafNode{};
                leaf->node_id = pid;
                leaf->parent_node_id = INVALID_VALUE;
                leaf->level = 0;
                leaf->count = 0;
                leaf->next = INVALID_VALUE;
                leaf->insert(key, value);
                buffer_manager.flushPage(pid);
                return;
            }

            // Non-empty: fix current root
            SlottedPage &page = buffer_manager.fix_page(*root);
            auto *node = reinterpret_cast<Node*>(page.page_data.get());

            if (node->is_leaf()) {
                auto *leaf = reinterpret_cast<LeafNode*>(node);
                if (leaf->count < LeafNode::kCapacity) {
                    leaf->insert(key, value);
                    buffer_manager.flushPage(*root);
                    return;
                }

                // --- Split leaf ---
                uint64_t new_pid = next_page_id++;
                SlottedPage &new_page = buffer_manager.fix_page(new_pid);
                auto *right = reinterpret_cast<LeafNode*>(new_page.page_data.get());
                *right = LeafNode{};

                size_t mid = LeafNode::kCapacity / 2;
                size_t right_count = LeafNode::kCapacity - mid;

                for (size_t i = 0; i < right_count; ++i) {
                    right->keys[i] = leaf->keys[mid + i];
                    right->values[i] = leaf->values[mid + i];
                }
                right->count = right_count;
                right->next = leaf->next;
                right->node_id = new_pid;
                right->parent_node_id = INVALID_VALUE;
                right->level = 0;

                leaf->count = mid;
                leaf->next = new_pid;

                buffer_manager.flushPage(leaf->node_id);
                buffer_manager.flushPage(right->node_id);

                // Promote first key of right leaf
                KeyT promote_key = right->keys[0];

                // Create new root inner node
                uint64_t root_pid = next_page_id++;
                SlottedPage &root_page = buffer_manager.fix_page(root_pid);
                auto *inner = reinterpret_cast<InnerNode*>(root_page.page_data.get());
                *inner = InnerNode{};
                inner->node_id = root_pid;         // step 2 line
                inner->level = 1;
                inner->count = 2;
                inner->keys[0] = promote_key;
                inner->children[0] = leaf->node_id;
                inner->children[1] = right->node_id;

                leaf->parent_node_id  = root_pid;//
                right->parent_node_id = root_pid;//
                root = root_pid;

                // Insert the original (key,value) into the correct side
                if (key < promote_key) {
                    leaf->insert(key, value);
                    buffer_manager.flushPage(leaf->node_id);
                } else {
                    right->insert(key, value);
                    buffer_manager.flushPage(right->node_id);
                }
                                // Finally flush root (structure changed)
                buffer_manager.flushPage(*root);
                return;

            }
            else {
                // Root is inner: descend to proper leaf
                auto *inner = reinterpret_cast<InnerNode*>(node);
                size_t cidx = inner->find_child_index(key);
                uint64_t leaf_pid = inner->children[cidx];

                SlottedPage &lpage = buffer_manager.fix_page(leaf_pid);
                auto *leaf = reinterpret_cast<LeafNode*>(lpage.page_data.get());

                if (leaf->count < LeafNode::kCapacity) {
                    leaf->insert(key, value);
                    buffer_manager.flushPage(leaf->node_id);
                    return;
                }

                // Split this leaf under the inner root
                uint64_t new_pid = next_page_id++;
                SlottedPage &new_page = buffer_manager.fix_page(new_pid);
                auto *right = reinterpret_cast<LeafNode*>(new_page.page_data.get());
                *right = LeafNode{};

                size_t mid = LeafNode::kCapacity / 2;
                size_t right_count = LeafNode::kCapacity - mid;
                for (size_t i = 0; i < right_count; ++i) {
                    right->keys[i]   = leaf->keys[mid + i];
                    right->values[i] = leaf->values[mid + i];
                }
                right->count = right_count;
                right->next  = leaf->next;
                right->node_id = new_pid;
                //right->parent_node_id = *root;  // parent is the inner root
                right->parent_node_id = inner->node_id; // keep this pointing to the inner
                right->level = 0;

                leaf->count = mid;
                leaf->next  = new_pid;
                // **NEW**: persist both halves immediately
                buffer_manager.flushPage(leaf->node_id);
                buffer_manager.flushPage(right->node_id);


                KeyT promote_key = right->keys[0];

                // Insert separator into the inner root
                //inner->insert(promote_key, new_pid);
                inner->insert_at(cidx, promote_key, new_pid);
                // Insert the original (key,value) into the correct leaf
                if (key < promote_key) {
                    leaf->insert(key, value);
                    buffer_manager.flushPage(leaf->node_id);
                } else {
                    right->insert(key, value);
                    buffer_manager.flushPage(right->node_id);
                }
                buffer_manager.flushPage(*root);
                return;
            }
            
        }
    private:
        void splitNode(std::vector<std::shared_ptr<Node>> path,
                   std::shared_ptr<Node> node) {
            // TODO
            // HINT: Split a full node into two, redistribute keys (and children/values) and propagate
            // the key upward to the parent. Handle both leaf and inner cases here.
            // Root might require special handling.
            // Call this from insert(...).
            UNUSED(node);
            UNUSED(path);
        }
};

int main(int argc, char* argv[]) {
    bool execute_all = false;
    std::string selected_test = "-1";

    if(argc < 2) {
        execute_all = true;
    } else {
        selected_test = argv[1];
    }

    using BTree = BTree<uint64_t, uint64_t, std::less<uint64_t>, 1024>;

    // Test 1: InsertEmptyTree
    if(execute_all || selected_test == "1") {
        std::cout<<"...Starting Test 1"<<std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        ASSERT_WITH_MESSAGE(tree.root.has_value() == false,
            "tree.root is not nullptr");

        tree.insert(42, 21);

        ASSERT_WITH_MESSAGE(tree.root.has_value(),
            "tree.root is still nullptr after insertion");

        std::string test = "inserting an element into an empty B-Tree";

        // Fix root page and obtain root node pointer
        SlottedPage* root_page = &buffer_manager.fix_page(*tree.root);
        auto root_node = reinterpret_cast<BTree::Node*>(root_page->page_data.get());

        ASSERT_WITH_MESSAGE(root_node->is_leaf() == true,
            test + " does not create a leaf node.");
        ASSERT_WITH_MESSAGE(root_node->count == 1,
            test + " does not create a leaf node with count = 1.");

        std::cout << "\033[1m\033[32mPassed: Test 1\033[0m" << std::endl;
    }

    // Test 2: InsertLeafNode
    if(execute_all || selected_test == "2") {
        std::cout<<"...Starting Test 2"<<std::endl;
        BufferManager buffer_manager;
        BTree tree( buffer_manager);

        ASSERT_WITH_MESSAGE(tree.root.has_value() == false,
            "tree.root is not nullptr");

        for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
            tree.insert(i, 2 * i);
        }
        ASSERT_WITH_MESSAGE(tree.root.has_value(),
            "tree.root is still nullptr after insertion");

        std::string test = "inserting BTree::LeafNode::kCapacity elements into an empty B-Tree";

        SlottedPage* root_page = &buffer_manager.fix_page(*tree.root);
        auto root_node = reinterpret_cast<BTree::Node*>(root_page->page_data.get());
        auto root_inner_node = static_cast<BTree::InnerNode*>(root_node);

        ASSERT_WITH_MESSAGE(root_node->is_leaf() == true,
            test + " creates an inner node as root.");
        ASSERT_WITH_MESSAGE(root_inner_node->count == BTree::LeafNode::kCapacity,
            test + " does not store all elements.");

        std::cout << "\033[1m\033[32mPassed: Test 2\033[0m" << std::endl;
    }

    // Test 3: InsertLeafNodeSplit
    if (execute_all || selected_test == "3") {
        std::cout << "...Starting Test 3" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        ASSERT_WITH_MESSAGE(tree.root.has_value() == false,
            "tree.root is not nullptr");

        for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
            tree.insert(i, 2 * i);
        }
        ASSERT_WITH_MESSAGE(tree.root.has_value(),
            "tree.root is still nullptr after insertion");

        SlottedPage* root_page = &buffer_manager.fix_page(*tree.root);
        auto root_node = reinterpret_cast<BTree::Node*>(root_page->page_data.get());
        auto root_inner_node = static_cast<BTree::InnerNode*>(root_node);

        assert(root_inner_node->is_leaf());
        assert(root_inner_node->count == BTree::LeafNode::kCapacity);

        // Let there be a split...
        tree.insert(424242, 42);

        std::string test =
            "inserting BTree::LeafNode::kCapacity + 1 elements into an empty B-Tree";

        ASSERT_WITH_MESSAGE(tree.root.has_value() != false, test + " removes the root :-O");

        SlottedPage* root_page1 = &buffer_manager.fix_page(*tree.root);
        root_node = reinterpret_cast<BTree::Node*>(root_page1->page_data.get());
        root_inner_node = static_cast<BTree::InnerNode*>(root_node);

        ASSERT_WITH_MESSAGE(root_inner_node->is_leaf() == false,
            test + " does not create a root inner node");
        ASSERT_WITH_MESSAGE(root_inner_node->count == 2,
            test + " creates a new root with count != 2");

        std::cout << "\033[1m\033[32mPassed: Test 3\033[0m" << std::endl;
    }

    // Test 4: LookupEmptyTree
    if (execute_all || selected_test == "4") {
        std::cout << "...Starting Test 4" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        std::string test = "searching for a non-existing element in an empty B-Tree";

        ASSERT_WITH_MESSAGE(tree.lookup(42).has_value() == false,
            test + " seems to return something :-O");

        std::cout << "\033[1m\033[32mPassed: Test 4\033[0m" << std::endl;
    }

    // Test 5: LookupSingleLeaf
    if (execute_all || selected_test == "5") {
        std::cout << "...Starting Test 5" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        // Fill one page
        for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
            tree.insert(i, 2 * i);
            ASSERT_WITH_MESSAGE(tree.lookup(i).has_value(),
                "searching for the just inserted key k=" + std::to_string(i) + " yields nothing");
        }

        // Lookup all values
        for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
            auto v = tree.lookup(i);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
            ASSERT_WITH_MESSAGE(*v == 2 * i, "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
        }

        std::cout << "\033[1m\033[32mPassed: Test 5\033[0m" << std::endl;
    }

    // Test 6: LookupSingleSplit
    if (execute_all || selected_test == "6") {
        std::cout << "...Starting Test 6" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        // Insert values
        for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
            tree.insert(i, 2 * i);
        }

        tree.insert(BTree::LeafNode::kCapacity, 2 * BTree::LeafNode::kCapacity);
        ASSERT_WITH_MESSAGE(tree.lookup(BTree::LeafNode::kCapacity).has_value(),
            "searching for the just inserted key k=" + std::to_string(BTree::LeafNode::kCapacity + 1) + " yields nothing");

        // Lookup all values
        for (auto i = 0ul; i < BTree::LeafNode::kCapacity + 1; ++i) {
            auto v = tree.lookup(i);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
            ASSERT_WITH_MESSAGE(*v == 2 * i,
                "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
        }

        std::cout << "\033[1m\033[32mPassed: Test 6\033[0m" << std::endl;
    }

    // Test 7: LookupMultipleSplitsIncreasing
    if (execute_all || selected_test == "7") {
        std::cout << "...Starting Test 7" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);
        auto n = 40 * BTree::LeafNode::kCapacity;

        // Insert values
        for (auto i = 0ul; i < n; ++i) {
            tree.insert(i, 2 * i);
            ASSERT_WITH_MESSAGE(tree.lookup(i).has_value(),
                "searching for the just inserted key k=" + std::to_string(i) + " yields nothing");
        }

        // Lookup all values
        for (auto i = 0ul; i < n; ++i) {
            auto v = tree.lookup(i);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
            ASSERT_WITH_MESSAGE(*v == 2 * i,
                "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
        }
        std::cout << "\033[1m\033[32mPassed: Test 7\033[0m" << std::endl;
    }

    // Test 8: LookupMultipleSplitsDecreasing
    if (execute_all || selected_test == "8") {
        std::cout << "...Starting Test 8" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);
        auto n = 10 * BTree::LeafNode::kCapacity;

        // Insert values
        for (auto i = n; i > 0; --i) {
            tree.insert(i, 2 * i);
            ASSERT_WITH_MESSAGE(tree.lookup(i).has_value(),
                "searching for the just inserted key k=" + std::to_string(i) + " yields nothing");
        }

        // Lookup all values
        for (auto i = n; i > 0; --i) {
            auto v = tree.lookup(i);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
            ASSERT_WITH_MESSAGE(*v == 2 * i,
                "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
        }

        std::cout << "\033[1m\033[32mPassed: Test 8\033[0m" << std::endl;
    }

    // Test 9: LookupRandomNonRepeating
    if (execute_all || selected_test == "9") {
        std::cout << "...Starting Test 9" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);
        auto n = 10 * BTree::LeafNode::kCapacity;

        // Generate random non-repeating key sequence
        std::vector<uint64_t> keys(n);
        std::iota(keys.begin(), keys.end(), n);
        std::mt19937_64 engine(0);
        std::shuffle(keys.begin(), keys.end(), engine);

        // Insert values
        for (auto i = 0ul; i < n; ++i) {
            tree.insert(keys[i], 2 * keys[i]);
            ASSERT_WITH_MESSAGE(tree.lookup(keys[i]).has_value(),
                "searching for the just inserted key k=" + std::to_string(keys[i]) +
                " after i=" + std::to_string(i) + " inserts yields nothing");
        }

        // Lookup all values
        for (auto i = 0ul; i < n; ++i) {
            auto v = tree.lookup(keys[i]);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(keys[i]) + " is missing");
            ASSERT_WITH_MESSAGE(*v == 2 * keys[i],
                "key=" + std::to_string(keys[i]) + " should have the value v=" + std::to_string(2 * keys[i]));
        }

        std::cout << "\033[1m\033[32mPassed: Test 9\033[0m" << std::endl;
    }

    // Test 10: LookupRandomRepeating
    if (execute_all || selected_test == "10") {
        std::cout << "...Starting Test 10" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);
        auto n = 10 * BTree::LeafNode::kCapacity;

        // Insert & updated 100 keys at random
        std::mt19937_64 engine{0};
        std::uniform_int_distribution<uint64_t> key_distr(0, 99);
        std::vector<uint64_t> values(100);

        for (auto i = 1ul; i < n; ++i) {
            uint64_t rand_key = key_distr(engine);
            values[rand_key] = i;
            tree.insert(rand_key, i);

            auto v = tree.lookup(rand_key);
            ASSERT_WITH_MESSAGE(v.has_value(),
                "searching for the just inserted key k=" + std::to_string(rand_key) +
                " after i=" + std::to_string(i - 1) + " inserts yields nothing");
            ASSERT_WITH_MESSAGE(*v == i,
                "overwriting k=" + std::to_string(rand_key) + " with value v=" + std::to_string(i) +
                " failed");
        }

        // Lookup all values
        for (auto i = 0ul; i < 100; ++i) {
            if (values[i] == 0) {
                continue;
            }
            auto v = tree.lookup(i);
            ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
            ASSERT_WITH_MESSAGE(*v == values[i],
                "key=" + std::to_string(i) + " should have the value v=" + std::to_string(values[i]));
        }

        std::cout << "\033[1m\033[32mPassed: Test 10\033[0m" << std::endl;
    }

    // Test 11: Erase
    if (execute_all || selected_test == "11") {
        std::cout << "...Starting Test 11" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        // Insert values
        for (auto i = 0ul; i < 2 * BTree::LeafNode::kCapacity; ++i) {
            tree.insert(i, 2 * i);
        }

        // Iteratively erase all values
        for (auto i = 0ul; i < 2 * BTree::LeafNode::kCapacity; ++i) {
            ASSERT_WITH_MESSAGE(tree.lookup(i).has_value(), "k=" + std::to_string(i) + " was not in the tree");
            tree.erase(i);
            ASSERT_WITH_MESSAGE(!tree.lookup(i), "k=" + std::to_string(i) + " was not removed from the tree");
        }
        std::cout << "\033[1m\033[32mPassed: Test 11\033[0m" << std::endl;
    }

    // Test 12: Persistant Btree
    if (execute_all || selected_test == "12") {
        std::cout << "...Starting Test 12" << std::endl;
        unsigned long n =  10 * BTree::LeafNode::kCapacity;

        // Build a tree
        {
            BufferManager buffer_manager;
            BTree tree(buffer_manager);

            // Insert values
            for (auto i = 0ul; i < n; ++i) {
                tree.insert(i, 2 * i);
                ASSERT_WITH_MESSAGE(tree.lookup(i).has_value(),
                    "searching for the just inserted key k=" + std::to_string(i) + " yields nothing");
            }

            // Lookup all values
            for (auto i = 0ul; i < n; ++i) {
                auto v = tree.lookup(i);
                ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
                ASSERT_WITH_MESSAGE(*v == 2 * i,
                    "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
            }
        }

        // recreate the buffer manager and check for existence of the tree
        {
            BufferManager buffer_manager(false);
            BTree tree(buffer_manager);

            // Lookup all values
            for (auto i = 0ul; i < n; ++i) {
                auto v = tree.lookup(i);
                ASSERT_WITH_MESSAGE(v.has_value(), "key=" + std::to_string(i) + " is missing");
                ASSERT_WITH_MESSAGE(*v == 2 * i,
                    "key=" + std::to_string(i) + " should have the value v=" + std::to_string(2 * i));
            }
        }

        std::cout << "\033[1m\033[32mPassed: Test 12\033[0m" << std::endl;
    }

    // Test 13: RangeQuery
    if (execute_all || selected_test == "13") {
        std::cout << "...Starting Test 13" << std::endl;
        BufferManager buffer_manager;
        BTree tree(buffer_manager);

        for (auto i = 0ul; i < 5000; ++i) {
            tree.insert(i, 2 * i);
        }

        auto results = tree.rangeQuery(50, 1000);

        ASSERT_WITH_MESSAGE(!results.empty(), "rangeQuery returned nothing");
        ASSERT_WITH_MESSAGE(results.size() == 951,
                            "rangeQuery should return 951 keys from 50 to 1000 inclusive"
                            " returned " + std::to_string(results.size()));

        for (size_t j = 0; j < results.size(); j++) {
            auto [k, v] = results[j];
            ASSERT_WITH_MESSAGE(k == 50 + j, "unexpected key in rangeQuery");
            ASSERT_WITH_MESSAGE(v == 2 * k, "unexpected value in rangeQuery");
        }

        std::cout << "\033[1m\033[32mPassed: Test 13\033[0m" << std::endl;
    }
    return 0;
}