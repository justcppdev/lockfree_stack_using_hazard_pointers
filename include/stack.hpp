#include <atomic>
#include <memory>
#include <thread>

unsigned int const max_hazard_pointers = 100;
struct hazard_pointer_t {
	std::atomic<std::thread::id> id;
	std::atomic<void *> pointer;
};

hazard_pointer_t hazard_pointers[ max_hazard_pointers ];

class hp_owner_t {
private:
	hazard_pointer_t * m_hp;
public:
	hp_owner_t();
	~hp_owner_t();
	auto get_pointer() -> std::atomic<void *> &;
};

hp_owner_t::hp_owner_t() :
	m_hp{ nullptr }
{
	for( unsigned int i = 0; i < max_hazard_pointers; ++i ) {
		std::thread::id old_id;
		if( hazard_pointers[ i ].id.compare_exchange_strong( old_id, std::this_thread::get_id() ) ) {
			m_hp = &hazard_pointers[ i ];
			break;
		}
	}
	if( !m_hp ) {
		throw std::runtime_error{ "No hazard pointer available" };
	}
}

hp_owner_t::~hp_owner_t()
{
	m_hp->pointer.store( nullptr );
	m_hp->id.store( std::thread::id{} );
}

auto hp_owner_t::get_pointer() -> std::atomic<void *> &
{
	return m_hp->pointer;
}

auto get_hazard_pointer_for_current_thread() -> std::atomic<void *> &
{
	thread_local static hp_owner_t hazard;
	return hazard.get_pointer();
}

bool outstanding_hazard_pointers_for( void * ptr )
{
	for( unsigned int i = 0; i < max_hazard_pointers; ++i ) {
		if( hazard_pointers[ i ].pointer.load() == ptr ) {
			return true;
		}
	}

	return false;
}

template< typename T >
void do_delete( void * ptr )
{
	delete static_cast<T *>( ptr );
}

struct data_to_reclaim_t {
	void * data;
	std::function<void( void * )> deleter;
	data_to_reclaim_t * next;

	template< typename T >
	data_to_reclaim_t( T * ptr );
	~data_to_reclaim_t();
};


template< typename T >
data_to_reclaim_t::data_to_reclaim_t( T * ptr ) :
	data{ ptr },
	deleter{ &do_delete<T> },
	next{ nullptr }
{
}

data_to_reclaim_t::~data_to_reclaim_t()
{
	deleter( data );
}

std::atomic<data_to_reclaim_t *> nodes_to_reclaim{ nullptr };

void add_to_reclaim_list( data_to_reclaim_t * node )
{
	node->next = nodes_to_reclaim.load();
	while( !nodes_to_reclaim.compare_exchange_weak( node->next, node ) ) {
		continue;
	}
}

template< typename T >
void reclaim_later( T * data )
{
	add_to_reclaim_list( new data_to_reclaim_t{ data } );
}

void delete_nodes_with_no_hazard_pointers()
{
	data_to_reclaim_t * current = nodes_to_reclaim.exchange( nullptr );
	while( current ) {
		data_to_reclaim_t * const next = current->next;
		if( !outstanding_hazard_pointers_for( current->data ) ) {
			delete current;
		}
		else {
			add_to_reclaim_list( current );
		}
		
		current = next;
	}
}

namespace jcd {
    template< typename T >
    class stack_t {
    private:
        struct node_t {
            std::shared_ptr<T> data;
            node_t * next;
            node_t( T const & data_ ) :
                data{ std::make_shared<T>( data_ ) } {
            }
        };

        std::atomic<node_t *> m_head;
    public:
        stack_t();
        void push( T const & data );
        auto pop() -> std::shared_ptr<T>;
        bool empty() const;
    };
    
    template< typename T >
    stack_t<T>::stack_t() :
        m_head{ nullptr }
    {
    }

    template< typename T >
    void stack_t<T>::push( T const & data )
    {
        node_t * const new_node = new node_t( data );
        new_node->next = m_head.load();
        while( !m_head.compare_exchange_weak( new_node->next, new_node ) ) {
            continue;
        }
    }

    template< typename T >
    auto stack_t<T>::pop() -> std::shared_ptr<T>
    {
        std::atomic<void *> & hp = get_hazard_pointer_for_current_thread();
        node_t * old_head = m_head.load();
        do {
            node_t * temp;
            do {
                temp = old_head;
                hp.store( old_head );
                old_head = m_head.load();
            } while( old_head != temp );

        } while( old_head && !m_head.compare_exchange_strong( old_head, old_head->next ) );

        hp.store( nullptr );

        std::shared_ptr<T> res;
        if( old_head ) {
            res.swap( old_head->data );

            if( outstanding_hazard_pointers_for( old_head ) ) {
                reclaim_later( old_head );
            } else {
                delete old_head;
            }

            delete_nodes_with_no_hazard_pointers();
        }

        return res;
    }
    
    template< typename T >
    bool stack_t<T>::empty() const
    {
        return m_head.load() == nullptr;
    }
}
