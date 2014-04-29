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
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return '<{0}>'.format(v_array_str(self.val))

class varrayPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return '{{{0}}}'.format(v_array_str(self.val))


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter('ntracer')
    pp.add_printer('vector','^impl::vector<.+>$',vectorPrinter)
    pp.add_printer('v_array','^impl::v_array<.+>$',varrayPrinter)
    return pp


gdb.printing.register_pretty_printer(
    gdb.current_objfile(),
    build_pretty_printer())