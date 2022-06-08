// Namespaced Header

#ifndef __NS_SYMBOL
// We need to have multiple levels of macros here so that __NAMESPACE_PREFIX_ is
// properly replaced by the time we concatenate the namespace prefix.
#define __NS_REWRITE(ns, symbol) ns ## _ ## symbol
#define __NS_BRIDGE(ns, symbol) __NS_REWRITE(ns, symbol)
#define __NS_SYMBOL(symbol) __NS_BRIDGE(Mlive, symbol)
#endif


// Classes
#ifndef CCHTTPConnection
#define CCHTTPConnection __NS_SYMBOL(CCHTTPConnection)
#endif

#ifndef CCMLiveMovieWriter
#define CCMLiveMovieWriter __NS_SYMBOL(CCMLiveMovieWriter)
#endif

