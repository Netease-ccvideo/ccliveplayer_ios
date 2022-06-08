# This script is a modified version of this: https://github.com/jverkoey/nimbus/blob/master/scripts/generate_namespace_header

header=${SRCROOT}/${PRODUCT_NAME}/NamespacedDependencies.h
prefix="Mlive"

grep "__NS_SYMBOL" $header
if [ $? -eq 0 ]; then
	exit 0
fi

echo "Generating $header from $CODESIGNING_FOLDER_PATH..."

echo "// Namespaced Header

#ifndef __NS_SYMBOL
// We need to have multiple levels of macros here so that __NAMESPACE_PREFIX_ is
// properly replaced by the time we concatenate the namespace prefix.
#define __NS_REWRITE(ns, symbol) ns ## _ ## symbol
#define __NS_BRIDGE(ns, symbol) __NS_REWRITE(ns, symbol)
#define __NS_SYMBOL(symbol) __NS_BRIDGE($prefix, symbol)
#endif

" > $header

# The following one-liner is a bit of a pain in the ass.
# Breakdown:
#
# nm $CODESIGNING_FOLDER_PATH -j
# Dump all of the symbols from the compiled library. This will include all UIKit
# and Foundation symbols as well.
#
# | grep "^_OBJC_CLASS_$_"
# Filter out the interfaces.
#
# | grep -v "\$_NS"
# Remove all Foundation classes.
#
# | grep -v "\$_UI"
# Remove all UIKit classes.
#
# | sed -e 's/_OBJC_CLASS_\$_\(.*\)/#ifndef \1\'$'\n''#define \1 __NS_SYMBOL(\1)\'$'\n''#endif/g'
# I use the syntax outlined here:
# http://stackoverflow.com/questions/6761796/bash-perl-or-sed-insert-on-new-line-after-found-phrase
# to create newlines so that we can write the following on separate lines:
#
#  #ifndef ...
#  #define ...
#  #endif
#
lib_file_path=$CODESIGNING_FOLDER_PATH/$PRODUCT_NAME

echo "// Classes" >> $header

nm $lib_file_path -j | sort | uniq | grep "^_OBJC_CLASS_\$_CC" | sed -e 's/_OBJC_CLASS_\$_\(.*\)/#ifndef \1\'$'\n''#define \1 __NS_SYMBOL(\1)\'$'\n''#endif\'$'\n''/g' >> $header

#echo "// Functions" >> $header

#nm $lib_file_path | sort | uniq | grep " T " | cut -d' ' -f3 | grep -v "\$_CCR" | grep -v "\$_CC" | sed -e 's/_\(.*\)/#ifndef \1\'$'\n''#define \1 __NS_SYMBOL(\1)\'$'\n''#endif\'$'\n''/g' >> $header

#echo "// Externs" >> $header

#nm $lib_file_path | sort | uniq | grep " D " | cut -d' ' -f3 | grep -v "_CC" | grep -v "CCR" | grep -v "CC" | grep -v "cc"  | sed -e 's/_\(.*\)/#ifndef \1\'$'\n''#define \1 __NS_SYMBOL(\1)\'$'\n''#endif\'$'\n''/g' >> $header

#nm $lib_file_path | sort | uniq | grep " S " | cut -d' ' -f3 | grep -v "_CC" | grep -v "CCR" | grep -v "CC" | grep -v "cc"  | sed -e 's/_\(.*\)/#ifndef \1\'$'\n''#define \1 __NS_SYMBOL(\1)\'$'\n''#endif\'$'\n''/g' >> $header
