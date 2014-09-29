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