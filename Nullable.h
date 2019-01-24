#pragma once

template <typename T>
class Nullable {
public:
    Nullable(T val) : value(val) {}
    Nullable() {
        value = GetNullValue();
    }

    Nullable<T> operator+(const Nullable<T>& other) const {
        if (!HasValue() || !other.HasValue())
            return Nullable<T>();
        else return Nullable<T>(value + other.value);
    }
    Nullable<T> operator+(const T& other) const {
        return operator+(Nullable<T>(other));
    }

    bool operator<=(const  Nullable<T>&  other) {
        if (!HasValue())
            return true;
        else return value <= other.value;
    }

    bool operator<=(const  T&  other) {
        return operator<=(Nullable<T>(other));
    }

    bool operator>=(const  Nullable<T>&  other) {
        if (!HasValue())
            return false;
        else return value >= other.value;
    }

    bool operator>=(const  T&  other) {
        return operator>=(Nullable<T>(other));
    }


    template <typename U>
    friend std::ostream& operator<< (std::ostream & os, const Nullable<U>  & obj);

    Nullable<T> Max(const Nullable<T>& other) const {
        if (!HasValue())
            return other;
        else if (!other.HasValue())
            return *this;
        else return Nullable<T>(std::max(value, other.value));
    }

    bool HasValue() const { return value != GetNullValue(); }
    T GetValue() const { return value; }

private:
    static T GetNullValue() { return std::numeric_limits<T>::max(); }

    T value;
};