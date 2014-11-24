import gdb.printing

def v_array_str(val):
    store = val.type.template_argument(0)
    store_v = val['store']
    items = store_v['items']
    
    if store.tag.startswith('fixed::item_store'):
        size = int(store.template_argument(0))
        items = items.cast(items.type.target().pointer())
    else:
        size = int(store_v['size'])

    return ','.join(str((items + i).dereference()) for i in range(size))

class vectorPrinter:
    def __init__(self,val):
        self.val = val

    def to_string(self):
        return '<{0}>'.format(v_array_str(self.val))

class varrayPrinter:
    def __init__(self,val):
        self.val = val

    def to_string(self):
        return '{{{0}}}'.format(v_array_str(self.val))

class vtypePrinter:
    def __init__(self,val):
        self.val = val
    
    def to_string(self):
        return str(self.val['data']['p'])


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter('ntracer')
    pp.add_printer('vector','^impl::vector<.+>$',vectorPrinter)
    pp.add_printer('v_array','^impl::v_array<.+>$',varrayPrinter)
    pp.add_printer('v_type','^simd::(?:float|double|int(?:8|16|32|64))_v_(?:128|256|512)$',vtypePrinter)
    return pp


gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer(),
    True)

try:
    import gdb.xmethod
except ImportError:
    pass
else:
    class XMethod(gdb.xmethod.XMethod):
        def __init__(self,name,meth,worker):
            gdb.xmethod.XMethod.__init__(self,name)
            self.meth = meth
            self.worker = worker
    
    class XMethodWorker(gdb.xmethod.XMethodWorker):
        def __init__(self,ctype):
            gdb.xmethod.XMethodWorker.__init__(self)
            self.ctype = ctype
            
        def get_arg_types(self):
            return ()
    
    class PyPtrMatcher(gdb.xmethod.XMethodMatcher):
        def __init__(self,name,type_prefix,methods):
            gdb.xmethod.XMethodMatcher.__init__(self,name)
            self.methods = list(methods)
            self.mmap = {}
            for m in self.methods:
                if m.meth not in self.mmap: self.mmap[m.meth] = []
                self.mmap[m.meth].append(m)
        
        def match(self,ctype,mname):
            if not ctype.tag.startswith(self.type_prefix): return None
            return [m.worker(ctype) for m in self.mmap[mname] if m.enabled]


    class PyPtrWorker_arrow(XMethodWorker):
        def __call__(self,obj):
            return obj['_obj']['_ptr'].cast(self.ctype.template_argument(0).pointer())
    
    class TProtoBase_getbase(XMethodWorker):
        def __call__(self,obj):
            r = obj['base']
            if self.ctype.template_argument(2) == False:
                r = r['_M_t']['_M_head_impl'].dereference()
            return r

    
    gdb.xmethod.register_xmethod_matcher(None,
        PyPtrMatcher('PyPtrMatcher','py::pyptr<',[XMethod('arrow','operator->',PyPtrWorker_arrow)]),True)
    
    gdb.xmethod.register_xmethod_matcher(None,
        PyPtrMatcher('TProtoBaseMatcher','tproto_base<',[XMethod('getbase','get_base',TProtoBase_getbase)]),True)

