@interface Foo : Object
{
	integer x;
}
-run;
@end


@implementation Foo

+load
{
	print ("+load\n");
	return self;
}

+alloc
{
	print ("+alloc\n");
	return class_create_instance (self);
}

-init
{
	print ("-init\n");
	return [super init];
}

+ (void) initialize
{
	print ("+initialize\n");
}

-run
{
	print ("Hello world\n");
	printf ("%i %p\n", self, &self.x);
}

@end
