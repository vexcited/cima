#include <cima/dsl.h>
#include <string.h>

// z.enum(["admin", "member", "guest"]): `enum Role { Role_admin, Role_member, Role_guest }`, and Role_name().
enumeration(Role, admin, member, guest);

object(Address,
  string(at_least(1))            street,
  string(at_least(1))            city,
  string(matches("^[0-9]{5}$"))  zip,
);

// a field list in a plain macro is how extend works, like zod's .extend() or typebox's Composite. a named type
// alone goes in parentheses, `(Role) role` is a reference to Role_schema.
#define BASE_USER_FIELDS                                                                                         \
  string(at_least(1), at_most(64))  name,                                                                        \
  string(email())                   email,                                                                       \
  (Role)                            role,                                                                        \
  optional(Address)                 address

object(User,
  u32() id,
  BASE_USER_FIELDS,
  options(strict(), rename_all(CAMEL)),
);

object(NewUser, BASE_USER_FIELDS);

// zod's .partial(): every field optional, for PATCH bodies.
object(UserPatch, partial(BASE_USER_FIELDS));

// zod's .refine() on the object: validate() declares `static bool passwords_match(const SignUp *, cima_ctx *)`,
// a plain function defined below.
object(SignUp,
  string(email())         email,
  string(at_least(12))    password,
  string()                confirm,
  validate(passwords_match),
);

static bool passwords_match(const SignUp *value, cima_ctx *ctx) {
  if (strcmp(value->password, value->confirm) == 0)
    return true;
  cima_report(ctx, "/confirm", "passwords do not match");
  return false;
}

// z.discriminatedUnion("type", [...]): a tagged c union, {"type": "circle", "radius": 2} on the wire.
object(Circle, number(gt(0)) radius);
object(Rect, number(gt(0)) width, number(gt(0)) height);
tagged_union(Shape, "type",
  (Circle)  circle,
  (Rect)    rect,
);

// z.lazy: a tree. `lazy` names `struct Category` while it is still being defined.
object(Category,
  string()                              name,
  array(lazy(Category), fallback([]))   children,
);

// arrays of arrays get an anonymous struct, declare those through CIMA_CTYPE or typeof.
object(Matrix, array(array(number()), nonempty()) rows);

static double area(const Shape *shape) {
  switch (shape->kind) {
  case Shape_circle:
    return 3.14159 * shape->circle.radius * shape->circle.radius;
  case Shape_rect:
    return shape->rect.width * shape->rect.height;
  }
  return 0;
}

int main(void) {
  double a = 0;
  if_parsed(Shape, shape, "{\"type\": \"rect\", \"width\": 2, \"height\": 3}") {
    a = area(&shape);
  }

  // only the fields that were sent are `some`.
  bool patched = false;
  if_parsed(UserPatch, patch, "{\"name\": \"Grace\", \"role\": \"admin\"}") {
    patched = patch.name.some && !patch.email.some && strcmp(Role_name(patch.role.value), "admin") == 0;
  }

  return a == 6.0 && patched ? 0 : 1;
}
