/*
	Entity.h

	(description)

	Copyright (C) 2001 Jeff Teunissen <deek@d2dc.net>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2 of
	the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/
#ifdef HAVE_CONFIG_H
# include "Config.h"
#endif

#import <Foundation/NSObject.h>

@class NSArray, NSDictionary;

// an Entity is a list of brush objects, with additional key / value info
@interface Entity: NSObject <NSCopying, NSMutableCopying>
{
	id		brushes;
	id		fields;
}

- initWithClassname: (NSString *)classname;
- initWithTokens;

- (NSString *) targetname;

- writeToFile: (NSString *) filename region: (BOOL) reg;

- (id) objectForKey: (NSString *) k;

- (Brush *) brushAtIndex: (unsigned) index;
- (id) fieldForKey: (
@end

@class NSMutableArray, NSMutableDictionary;

@interface MutableEntity: Entity
{
}

@end
