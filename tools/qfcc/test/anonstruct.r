void printf (string fmt, ...) = #0;

typedef struct xyzzy_s {
    int         magic;
} xyzzy_t;

typedef struct anon_s {
	int         foo;
	int         id;
	struct {
		int         bar;
		int         baz;
	};
	union {
		int         snafu;
		float       fizzle;
	};
} anon_t;

union {
	int         xsnafu;
	float       xfizzle;
};

int foo (float f)
{
	anon_t anon;
	anon.fizzle = f;
	return anon.snafu;
}

int main()
{
	anon_t anon;
	int         ret = 0;
	if (&anon.snafu != &anon.fizzle) {
		printf ("anon union broken: %p %p\n",
				&anon.snafu, &anon.fizzle);
		ret |= 1;
	}
	if (&anon.snafu - &anon.baz != 1) {
		printf ("snafu and baz not adjacant: snafu:%p baz:%p\n",
				&anon.snafu, &anon.baz);
		ret |= 1;
	}
	if (&anon.baz - &anon.bar != 1) {
		printf ("baz and bar not adjacant: baz:%p bar:%p\n",
				&anon.baz, &anon.bar);
		ret |= 1;
	}
	if (&anon.bar - &anon.id != 1) {
		printf ("bar not after id: bar:%p id:%p\n",
				&anon.bar, &anon.id);
		ret |= 1;
	}
	return ret;
}
