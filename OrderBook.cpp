#include<iostream>
#include<map>
#include<set>
#include<list>
#include<cmath>
#include<ctime>
#include<deque>
#include<queue>
#include<stack>
#include<limits>
#include<string>
#include<vector>
#include<numeric>
#include<iostream>
#include<algorithm>
#include<unordered_map>
#include<memory>
#include<variant>
#include<optional>
#include<tuple>
#include<format>

enum class OrderType {
    GoodTillCancel,
    FillAndKill
};

enum class Side {
    Buy, Sell
};


using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

//gets information about the state of the order book
struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos{
    public:
        OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks) : bids_ {bids}, asks_{asks} {

        }

        const LevelInfos& GetBids() const { return bids_;}
        const LevelInfos& GetAsks() const {return asks_;}

    private:
        LevelInfos bids_;
        LevelInfos asks_;
};

class Order {
    public: 
        Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity) : 
        orderType_ {orderType},
        orderId_ {orderId},
        side_ {side},
        price_ {price},
        initialQuantity_ {quantity},
        remainingQuantity_ {quantity}
        {

        }

        OrderId GetOrderId() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        OrderType GetOrderType() const { return orderType_; }
        Quantity GetInitialQuantity() const { return initialQuantity_; }
        Quantity GetRemainingQuantity() const { return remainingQuantity_; }

        Quantity GetFilledQuantity() const {return GetInitialQuantity() - GetRemainingQuantity();}
        
        void Fill(Quantity quantity) {
            if(quantity > GetRemainingQuantity()) {
                throw std::logic_error("Order ({}) cannot be filled for more than its remaining quantity. " + GetOrderId());
            }

            remainingQuantity_ -= quantity;
        }


    private:
        OrderType orderType_;
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity initialQuantity_;
        Quantity remainingQuantity_;
};

//
using OrderPointer = std::shared_ptr<Order>;
//we use list because list gives iterator that cannot be invalidated regardless of size; this is useful to determine where our order is in the bid or ask book
//list is dispersed in memeroy while veector is together
using OrderPointers = std::list<OrderPointer>;


class OrderModify
{
    public:
        OrderModify(OrderId orderId, Side side, Price price, Quantity quantity) :
                                                                                  orderId_{orderId},
                                                                                  side_{side},
                                                                                  price_{price},
                                                                                  quantity_ {quantity}
        {

        }
        OrderId GetOrderId() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        Quantity GetQuantity() const { return quantity_; }

        OrderPointer ToOrderPointer(OrderType type) const {
            return std::make_shared<Order>(type, orderId_, side_, price_, quantity_);
        }


    private:
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity quantity_;
};

int main(){

    return 0;
}