#include <iostream>
#include <sstream>
#include <vector>
#include <map>

#define DEFAULT_OBJECT_SEMANTICS(TYPE)    \
TYPE(TYPE const &)             = delete;  \
TYPE & operator=(TYPE const &) = delete;  \
TYPE(TYPE&&)                   = default; \
TYPE & operator=(TYPE&&)       = default; \

using message_tokens_t = std::vector<std::string>;

struct Order
{
    std::string Side;
    std::string TimeInForce;
    uint64_t    Price;
    uint64_t    Quantity;
    std::string ID;
    
    //default object semantics w/ a ctor for message tokens
    explicit Order(message_tokens_t const & messageTokens)
    :Side        (messageTokens.at(0)),
    TimeInForce (messageTokens.at(1)),
    Price       (-1),
    Quantity    (-1),
    ID          (messageTokens.at(4))
    {
        std::stringstream(messageTokens.at(2)) >> Price;
        std::stringstream(messageTokens.at(3)) >> Quantity;
    }
    Order():Side(), TimeInForce(), Price(-1), Quantity(-1), ID(){}
    DEFAULT_OBJECT_SEMANTICS(Order)
    ~Order(){}
};

struct OrderBook
{
    //public methods
    //processNewBuyOrder
    //processNewSelOrder
    //processCancel
    //processMod
    //printBook
    //ctors/assg/dtor w/ object semantics
    
    //processing a new order involves trying to match it against the current book
    //and then giving it an order finder and sticking it in the book if
    //there's anything left after the attempt to match
    //book orders and their find information are removed if they are fully matched
    //during the match process
    
    //cancelling causes no match, so we just retreive the order and throw it away
    //modifies, b/c they can switch sides and match, are done as a cancel, but then
    //we alter the retrieve order, and push it back into processNew...
    
    using price_t          = uint64_t;
    using order_vector_t   = std::vector<Order>;
    using order_id_t       = std::string;
    
    using bid_book_t       = std::map<price_t, order_vector_t, std::greater<price_t>>;
    using ask_book_t       = std::map<price_t, order_vector_t>;
    using order_finder_t   = Order (OrderBook::*)(price_t, order_id_t const &);
    using order_map_t      = std::map<order_id_t, std::pair<order_finder_t, price_t>>;
    
    void processNewBuyOrder(Order && newOrder)
    {
        tryMatchOrder(newOrder, mAsks);
        if (newOrder.TimeInForce=="GFD" && newOrder.Quantity > 0)
        {
            mOrderFinders[newOrder.ID] = std::make_pair(&OrderBook::bidOrderFinder, newOrder.Price);
            mBids[newOrder.Price].push_back(std::move(newOrder));
        }
        //checkFullFinderConsistency(); ok
        //checkForZeroSizeOrders(); ok
    }
    void processNewSelOrder(Order && newOrder)
    {
        tryMatchOrder(newOrder, mBids);
        if (newOrder.TimeInForce=="GFD" && newOrder.Quantity > 0)
        {
            mOrderFinders[newOrder.ID] = std::make_pair(&OrderBook::askOrderFinder, newOrder.Price);
            mAsks[newOrder.Price].push_back(std::move(newOrder));
        }
        //checkFullFinderConsistency(); ok
        //checkForZeroSizeOrders(); ok
    }
    void processCancel(order_id_t const & orderID)
    {//will create no matches; all we have to do is remove it
        if (mOrderFinders.find(orderID)==mOrderFinders.end()) return; //we don't know this order
        retrieveOrder(orderID);
        mOrderFinders.erase(orderID);
        //checkFullFinderConsistency(); ok
    }
    void processMod(message_tokens_t const & modMessageTokens)
    {//retrieve it (which removes it), modify its info, and process as if a new order
        auto const orderID = modMessageTokens.at(1);
        if (mOrderFinders.find(orderID)==mOrderFinders.end()) return; //we don't know this order
        
        auto order = retrieveOrder(orderID);
        mOrderFinders.erase(order.ID); //no longer there; we have moved it to here
        
        //alter this order for resubmission
        order.Side = modMessageTokens.at(2);
        std::stringstream(modMessageTokens.at(3)) >> order.Price;
        std::stringstream(modMessageTokens.at(4)) >> order.Quantity;
        //GFD stays the same and b/c id is the same, find info will be overwritten appropriately
        //so we don't remove it as we did in processCancel
        if (order.Side=="BUY") processNewBuyOrder(std::move(order));
        else                   processNewSelOrder(std::move(order));
    }
    void printBook() const
    {//both are to be descending, so the loop specification is different
        std::cout << "SELL:" << std::endl;
        for (auto iter = mAsks.rbegin(); iter != mAsks.rend(); ++iter)
        {
            printLevel(iter);
        }
        std::cout << "BUY:" << std::endl;
        for (auto iter = mBids.begin(); iter != mBids.end(); ++iter)
        {
            printLevel(iter);
        }
    }
    
    //default object semantics
    OrderBook():mBids(), mAsks(), mOrderFinders(){}
    DEFAULT_OBJECT_SEMANTICS(OrderBook)
    ~OrderBook(){}
    
private:
    Order retrieveOrder(order_id_t const & orderID)
    {
        auto const findTools = mOrderFinders.at(orderID);
        auto const finder    = findTools.first;
        auto const price     = findTools.second;
        //checkForBadFinders();
        return (this->*finder)(price, orderID);
    }
    Order bidOrderFinder(price_t priceLevel, order_id_t const & orderID)
    {
        auto returnOrder   = Order();
        auto & levelOrders = mBids.at(priceLevel);
        returnOrder        = retrieveOrderInLevelOrders(levelOrders, orderID);
        if (levelOrders.empty()) mBids.erase(priceLevel);
        return returnOrder;
    }
    Order askOrderFinder(price_t priceLevel, order_id_t const & orderID)
    {
        auto returnOrder   = Order();
        auto & levelOrders = mAsks.at(priceLevel);
        returnOrder        = retrieveOrderInLevelOrders(levelOrders, orderID);
        if (levelOrders.empty()) mAsks.erase(priceLevel);
        return returnOrder;
    }
    Order retrieveOrderInLevelOrders(order_vector_t & levelOrders, order_id_t const & orderID)
    {//we only call this when we "know it's there"
        auto foundOrder = Order();
        auto findIter = std::find_if(levelOrders.begin(), levelOrders.end(),
                                     [&orderID](Order const & order)->bool
                                     {
                                         return (orderID==order.ID);
                                     });
        foundOrder = std::move(*findIter);
        levelOrders.erase(findIter);
        return foundOrder;
    }
    template <typename BookType>
    void tryMatchOrder(Order & newOrder, BookType & bookSide)
    {
        auto emptyLevelsEndIter = bookSide.begin();
        auto newOrderWillMatchBook = (newOrder.Side=="BUY") ? //side-dependent comparison
        [](uint64_t n, uint64_t b){return (n >= b);} :
        [](uint64_t n, uint64_t b){return (n <= b);} ;
        for (auto iter = bookSide.begin(); iter != bookSide.end(); ++iter)
        {
            auto const levelPrice = iter->first;
            if (newOrderWillMatchBook(newOrder.Price, levelPrice))
            {
                auto & level = iter->second;
                matchOrder(newOrder, level);
                if (level.empty())
                {
                    emptyLevelsEndIter = iter; ++emptyLevelsEndIter;
                }
                if (newOrder.Quantity == 0) break;
            }
        }
        bookSide.erase(bookSide.begin(), emptyLevelsEndIter);
    }
    void matchOrder(Order & newOrder, std::vector<Order> & bookLevel)
    {//orders are in time order at a price, and we know the prices cross
        //so we can just have at it
        //we'll keep an iter to one past fully matched orders, so we can remove them
        auto lastFullyMatchedEndIter = bookLevel.begin();
        for (auto iter = bookLevel.begin(); iter != bookLevel.end(); ++iter)
        {
            auto & bookOrder = *iter;
            auto const matchSize = (bookOrder.Quantity >= newOrder.Quantity ) ?
            (newOrder.Quantity) : (bookOrder.Quantity) ;
            printMatch(bookOrder, newOrder, matchSize);
            newOrder.Quantity  -= matchSize;
            bookOrder.Quantity -= matchSize;
            if (bookOrder.Quantity==0)//we want to remove totally matched orders
            {//I don't like splitting the removal, but we're trying to be quick
                mOrderFinders.erase(bookOrder.ID); //remove from finder book
                lastFullyMatchedEndIter = iter; ++lastFullyMatchedEndIter;
            }
            if (newOrder.Quantity == 0) break;
        }
        //now erase from book level vector
        bookLevel.erase(bookLevel.begin(), lastFullyMatchedEndIter);
    }
    void printMatch(Order const & bookOrder, Order const & newOrder, uint64_t const matchSize) const
    {
        std::cout << "TRADE " << bookOrder.ID << " " //we know book order came first
        << bookOrder.Price << " "
        << matchSize      << " "
        << newOrder.ID    << " "
        << newOrder.Price << " "
        << matchSize      << std::endl;
    }
    template <typename MapIter>
    void printLevel(MapIter const & iter) const
    {
        auto const & priceLevel = iter->second;
        auto totalQuantity = 0;
        for (auto const & o : priceLevel) totalQuantity += o.Quantity;
        std::cout << iter->first << " " << totalQuantity << std::endl;
    }
    
    //debugging methods
    void checkForCrossedBook() const
    {
        if (mBids.empty() || mAsks.empty()) return;
        auto const bestBid = mBids.begin()->first;
        auto const bestAsk = mAsks.begin()->first;
        if (bestAsk <= bestBid) throw;
    }
    void checkForFindersConsistency() const
    {
        for (auto const & kv : mOrderFinders)
        {
            auto const orderID = kv.first;
            auto const price   = kv.second.second;
            auto const finder  = [&orderID](Order const & o){return o.ID==orderID;};
            auto priceFound = false;
            if (mBids.find(price) != mBids.end())
            {
                priceFound = true;
                auto const & level = mBids.at(price);
                if (find_if(level.begin(), level.end(), finder)==level.end()) throw;
            }
            if (mAsks.find(price) != mAsks.end())
            {
                priceFound = true;
                auto const & level = mAsks.at(price);
                if (find_if(level.begin(), level.end(), finder)==level.end()) throw;
            }
            //if (!priceFound) throw; this goes beyond consistency; narrowed to this, so checkForBadFinders
        }
    }
    void checkForBadFinders() const
    {
        auto throwIfOrderNotElsewhere = false;
        for (auto const & kv : mOrderFinders)
        {
            auto const finderPrice = kv.second.second;
            auto const bidFindIter = mBids.find(finderPrice);
            auto const askFindIter = mAsks.find(finderPrice);
            if (bidFindIter==mBids.end() && askFindIter==mAsks.end()) throwIfOrderNotElsewhere = true;
            if (throwIfOrderNotElsewhere)
            {//ok, good; they should just simply not be here, as finders
                //for (auto const & pl : mBids)
                //  for (auto const & o : pl.second) if (o.ID == kv.first) throwIfOrderNotElsewhere = false;
                //for (auto const & pl : mAsks)
                //  for (auto const & o : pl.second) if (o.ID == kv.first) throwIfOrderNotElsewhere = false;
                break;
            }
        }
        if (throwIfOrderNotElsewhere) throw;
    }
    void checkFullFinderConsistency() const
    {
        //every order has a finder and it's got the right location
        auto foundIDs = std::vector<std::string>();
        checkBookAndAddIds(mBids, foundIDs);
        checkBookAndAddIds(mAsks, foundIDs);
        //and that's all the finders there are
        for (auto const id : foundIDs)
            if (mOrderFinders.find(id)==mOrderFinders.end()) throw;
    }
    template <typename BookType>
    void checkBookAndAddIds(BookType const & bookSide, std::vector<std::string> & foundIDs) const
    {
        for (auto const & kv : bookSide)
        {
            auto const price   = kv.first;
            auto const & level = kv.second;
            for (auto const & o : level)
            {
                auto findIter = mOrderFinders.find(o.ID);
                if (findIter == mOrderFinders.end()) throw;
                if (findIter->second.second != price) throw;
                foundIDs.push_back(o.ID);
            }
        }
    }
    void checkForZeroSizeOrders() const
    {
        checkForZeroSize(mBids);
        checkForZeroSize(mAsks);
    }
    template <typename BookType>
    void checkForZeroSize(BookType const & bookSide) const
    {
        for (auto const & kv : bookSide) for (auto const & o : kv.second) if (o.Quantity==0) throw;
    }
    
private:
    bid_book_t  mBids;
    ask_book_t  mAsks;
    order_map_t mOrderFinders;
};


struct MatchingEngine
{
    void processNextMessage(std::string const & message)
    {
        dispatchMessage(tokenizeMessage(message));
    }
    
    MatchingEngine():mOrderBook(){}
    DEFAULT_OBJECT_SEMANTICS(MatchingEngine)
    ~MatchingEngine(){}
private:
    void dispatchMessage(std::vector<std::string> const & messageTokens)
    {
        if (messageTokens.front()=="PRINT")
            printBook();
        else
            processOrderMessage(messageTokens);
    }
    void printBook(){mOrderBook.printBook();}
    void processOrderMessage(message_tokens_t const & messageTokens)
    {
        auto const & leadToken = messageTokens.front();
        if      (leadToken=="BUY")    mOrderBook.processNewBuyOrder(Order(messageTokens));
        else if (leadToken=="SELL")   mOrderBook.processNewSelOrder(Order(messageTokens));
        else if (leadToken=="MODIFY") mOrderBook.processMod(messageTokens);
        else if (leadToken=="CANCEL") mOrderBook.processCancel(messageTokens.at(1));
        else ; //throw runtime_error?
    }
    message_tokens_t tokenizeMessage(std::string const & message)
    {
        auto tokens           = message_tokens_t();
        auto tokenStart       = 0;
        auto const messageEnd = message.size();
        for (auto i = 0; i < messageEnd; ++i)
        {
            if (message[i]==' ')
            {
                tokens.push_back(message.substr(tokenStart, i - tokenStart));
                tokenStart = i + 1;
            }
        } 
        tokens.push_back(message.substr(tokenStart, messageEnd - tokenStart));
        return tokens;
    }
private:
    OrderBook mOrderBook;
};//end MatchingEngine

#undef DEFAULT_OBJECT_SEMANTICS


int main()
{
  auto messages = std::vector<std::string>();
  messages.push_back("BUY GFD 11 100 order1");
  messages.push_back("BUY GFD 10 200 order2");
  messages.push_back("MODIFY order2 SELL 10 1000");
  messages.push_back("PRINT");


  for (auto const & m : messages) std::cout << m << std::endl; 
  std::cout << std::endl << std::endl;

  auto engine = MatchingEngine();
  for (auto const & message : messages)
  {
    engine.processNextMessage(message);
  }

  std::cout << "matching engine exiting" << std::endl;
  return 0;
}


/*struct OrderBook
{
  void addBuy(Order && newOrder)
  {//iterate over the ask levels; we assume > 0 on newOrder
    auto fullyMatchedLevelsEndIter = mAsks.begin();
    for (auto iter = mAsks.begin(); iter != mAsks.end(); ++iter)
    {//if we cross the book, match against that level
      auto const bookLevelPrice = iter->first;
      if      (newOrder.Price >= bookLevelPrice)
      {
        matchOrders(iter->second, newOrder);//can alter both arguments
        if ((iter->second).empty())
        {//map iter doesn't seem to support " = iter + 1 "
          fullyMatchedLevelsEndIter = iter; ++fullyMatchedLevelsEndIter;
        }
        if (newOrder.Quantity==0) break; //if we fully matched, we're done 
      }
    }
    mAsks.erase(mAsks.begin(), fullyMatchedLevelsEndIter);
    if (newOrder.Quantity != 0 && newOrder.TimeInForce=="GFD")
    {
      mBids[newOrder.Price].push_back(std::move(newOrder));
    }
  }
  void addSel(Order && newOrder)
  {//see comments under addBuy; should unify this code if possible
    auto fullyMatchedLevelsEndIter = mBids.begin();
    for (auto iter = mBids.begin(); iter != mBids.end(); ++iter)
    {
      auto const bookLevelPrice = iter->first;
      if (newOrder.Price <= bookLevelPrice)
      {
        matchOrders(iter->second, newOrder);
        if ((iter->second).empty())
        {
          fullyMatchedLevelsEndIter = iter; ++fullyMatchedLevelsEndIter;
        }
        if (newOrder.Quantity==0) break;
      }
    }
    mBids.erase(mBids.begin(), fullyMatchedLevelsEndIter);
    if (newOrder.Quantity != 0 && newOrder.TimeInForce=="GFD")
    {
      mAsks[newOrder.Price].push_back(std::move(newOrder));
    }
  }
  void doMod(std::vector<std::string> const & messageTokens)
  {
    auto const & orderID = messageTokens.at(1);
    auto newTokens = messageTokens;
    newTokens.at(0) = newTokens.at(2);
    newTokens.at(1) = "GFD";
    newTokens.erase(newTokens.begin() + 2);
    newTokens.push_back(orderID); //now we have tokens for a new order
    doCancel(orderID);
    if (newTokens.at(0)=="BUY") addBuy(Order(newTokens));
    else                        addSel(Order(newTokens));
  }
  void doCancel(std::vector<std::string> const & messageTokens)
  {
    auto const & orderIDToCancel = messageTokens.at(1);
    doCancel(orderIDToCancel);
  }
  void doCancel(std::string const & orderIDToCancel)
  {
    auto bidIter = findAndRemoveOrder(mBids, orderIDToCancel);
    if (bidIter != mBids.end())
    {
      if ((bidIter->second).empty()) mBids.erase(bidIter);
      return;
    } 
    auto askIter = findAndRemoveOrder(mAsks, orderIDToCancel);
    if (askIter != mAsks.end())
    {
      if ((askIter->second).empty()) mAsks.erase(askIter);
      return;
    }
    else std::cout << "could not find order:  " << orderIDToCancel << std::endl;
  }
  template <typename MapType>
  typename MapType::iterator findAndRemoveOrder(MapType & map, std::string const & orderID)
  {
    for (auto iter = map.begin(); iter != map.end(); ++iter)
    {
      auto & level = iter->second;
      auto findIter = find_if(level.begin(), level.end(), 
      [&orderID](Order const & order)
      {
        return (order.ID == orderID);
      });
      if (findIter != level.end())
      {
        level.erase(findIter);
        return iter;
      }
    } 
    return map.end();
  }  

  void matchOrders(std::vector<Order> & bookLevel, Order & newOrder)
  {//orders are in time order at a price, and we know the prices cross
   //so we can just have at it; notice we don't have to care about
   //who's the buy and who's the sell here
   //we'll keep an iter to one past fully matched orders, so we can remove them
    auto lastFullyMatchedEndIter = bookLevel.begin();
    for (auto iter = bookLevel.begin(); iter != bookLevel.end(); ++iter)
    {
      auto & bookOrder = *iter;
      std::cout << "TRADE " << bookOrder.ID << " " //we know book o came first
                << bookOrder.Quantity << " "
                << bookOrder.Price    << " "     
                << newOrder.ID        << " "
                << newOrder.Quantity  << " "
                << newOrder.Price     << std::endl;
      auto const matchSize = (bookOrder.Quantity >= newOrder.Quantity ) ?
                             (newOrder.Quantity) : (bookOrder.Quantity) ;
      newOrder.Quantity  -= matchSize;
      bookOrder.Quantity -= matchSize;
      if (bookOrder.Quantity==0)
      {
        lastFullyMatchedEndIter = iter; ++lastFullyMatchedEndIter;
      } 
      if (newOrder.Quantity == 0) break;
    }
    bookLevel.erase(bookLevel.begin(), lastFullyMatchedEndIter);   
   }
  void printBook() const
  {//both sides are requested in decreasing order
    std::cout << "SELL:" << std::endl;
    for (auto iter = mAsks.rbegin(); iter != mAsks.rend(); ++iter)
    {
      auto const & priceLevel = iter->second;
      auto totalQuantity = 0;
      for (auto const & o : priceLevel) totalQuantity += o.Quantity;
      std::cout << iter->first << " " << totalQuantity << std::endl;
    }
    std::cout << "BUY:" << std::endl;
    for (auto iter = mBids.begin(); iter != mBids.end(); ++iter)
    {
      auto const & priceLevel = iter->second;
      auto totalQuantity = 0;
      for (auto const & o : priceLevel) totalQuantity += o.Quantity;
      std::cout << iter->first << " " << totalQuantity << std::endl;
    }
  }
private:
  std::map<uint64_t, std::vector<Order>, std::greater<uint64_t>> mBids;
  std::map<uint64_t, std::vector<Order>>                         mAsks;
};//end OrderBook
*/
