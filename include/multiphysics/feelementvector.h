
#ifndef A2D_FE_ELEMENT_VECTOR_H
#define A2D_FE_ELEMENT_VECTOR_H

#include "multiphysics/femesh.h"
#include "multiphysics/fesolution.h"

namespace A2D {

/*
  This file contains objects that enable a vector-centric view of the
  finite-element degrees of freedom.

  The element vector class must implement the following:

  1. A lightweight object ElementVector::FEDof that is designed to access
  the degrees of freedom associated with an element, without exposing the
  details of the underlying element vector implementation. This class must
  contain the following members:

  1.1 Constructor with no arguments
  1.2 T* get() and const T* get const () that retrieves a pointer to the
  element data

  2. get_element_values(elem, dof)

  This function gets the element degrees of freedom associated with the index
  elem, and sets them into the FEDof object.  The dof object may store a local
  copy of them or just contain a pointer to the array of element values.

  2. add_element_values(elem, dof)

  This function adds the values to the element vector. This function may be
  empty if the implementation uses references to the values.

  3. init_values()

  Initialize the values from the solution vector into any temporary or local
  storage in the vector.

  4. init_zero_values()

  Initialize and zero values from the temporary or local storage only. Note
  that this does not zero values in the source vector.

  5. add_values()

  Add values into the source vector from any local storage. This may be an
  empty function if the values are stored directly.
*/

/*
  In-place element vector implementation
*/
template <typename T, class Basis>
class ElementVector_InPlaceSerial {
 public:
  ElementVector_InPlaceSerial(A2D::ElementMesh<Basis>& mesh, A2D::SolutionVector<T>& vec)
      : mesh(mesh), vec(vec) {}

  // Required DOF container object (different for each element vector
  // implementation)
  class FEDof {
   public:
    FEDof() { std::fill(dof, dof + Basis::ndof, T(0.0)); }

    /**
     * @brief Get the values associated with the given basis
     *
     * @return A pointer to the degrees of freedom
     */
    template <A2D::index_t index>
    T* get() {
      return &dof[Basis::template get_dof_offset<index>()];
    }

    /**
     * @brief Get the values associated with the given basis
     *
     * @return A pointer to the degrees of freedom
     */
    template <A2D::index_t index>
    const T* get() const {
      return &dof[Basis::template get_dof_offset<index>()];
    }

   private:
    // Variables for all the basis functions
    T dof[Basis::ndof];
  };

  /**
   * @brief Initialize the element vector values
   *
   * This function may be called once before element values are accessed.
   */
  void init_values() {}

  /**
   * @brief Initialize any local element vector values to zero
   *
   * This function may be called before element values are added.
   */
  void init_zero_values() {}

  /**
   * @brief Finish adding values to the element vector
   *
   * Add any values from the element vector into the source vector.
   */
  void add_values() {}

  /**
   * @brief Get the element values from the object and store them in the FEDof
   *
   * @param elem the element index
   * @param dof the object that stores a reference to the degrees of freedom
   */
  // Get values for this element from the vector
  void get_element_values(A2D::index_t elem, FEDof& dof) {
    get_element_values_<Basis::nbasis - 1>(elem, dof);
  }

  /**
   * @brief Add the degree of freedom values to the element vector
   *
   * @param elem the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   * freeom
   *
   * If FEDof contains a pointer to data, this function may do nothing
   */
  // Add values for this element to the vector
  void add_element_values(A2D::index_t elem, const FEDof& dof) {
    add_element_values_<Basis::nbasis - 1>(elem, dof);
  }

 private:
  template <A2D::index_t basis>
  void get_element_values_(A2D::index_t elem, FEDof& dof) {
    T* values = dof.template get<basis>();
    for (A2D::index_t i = 0; i < Basis::template get_ndof<basis>(); i++) {
      const int sign = mesh.get_global_dof_sign(elem, basis, i);
      const A2D::index_t dof_index = mesh.get_global_dof(elem, basis, i);
      values[i] = sign * vec[dof_index];
    }
    get_element_values_<basis - 1>(elem, dof);
  }

  template <>
  void get_element_values_<0>(A2D::index_t elem, FEDof& dof) {
    T* values = dof.template get<0>();
    for (A2D::index_t i = 0; i < Basis::template get_ndof<0>(); i++) {
      const int sign = mesh.get_global_dof_sign(elem, 0, i);
      const A2D::index_t dof_index = mesh.get_global_dof(elem, 0, i);
      values[i] = sign * vec[dof_index];
    }
  }

  template <A2D::index_t basis>
  void add_element_values_(A2D::index_t elem, const FEDof& dof) {
    const T* values = dof.template get<basis>();
    for (A2D::index_t i = 0; i < Basis::template get_ndof<basis>(); i++) {
      const int sign = mesh.get_global_dof_sign(elem, basis, i);
      const A2D::index_t dof_index = mesh.get_global_dof(elem, basis, i);
      vec[dof_index] += sign * values[i];
    }
    add_element_values_<basis - 1>(elem, dof);
  }

  template <>
  void add_element_values_<0>(A2D::index_t elem, const FEDof& dof) {
    const T* values = dof.template get<0>();
    for (A2D::index_t i = 0; i < Basis::template get_ndof<0>(); i++) {
      const int sign = mesh.get_global_dof_sign(elem, 0, i);
      const A2D::index_t dof_index = mesh.get_global_dof(elem, 0, i);
      vec[dof_index] += sign * values[i];
    }
  }

  A2D::ElementMesh<Basis>& mesh;
  A2D::SolutionVector<T>& vec;
};

/*
  Kokkos implementation of the element vector type
*/
template <typename T, class Basis>
class ElementVector_Kokkos {
 public:
  ElementVector_Kokkos(ElementMesh<Basis>& mesh, SolutionVector<T>& vec)
      : mesh(mesh), vec(vec), element_dof("element_dof", mesh.get_num_elements()) {}

  // Required DOF container object (different for each element vector
  // implementation)
  class FEDof {
   public:
    FEDof() {}

    /**
     * @brief Get the values associated with the given basis
     *
     * @return A pointer to the degrees of freedom
     */
    template <A2D::index_t index>
    T* get() {
      return dof[index];
    }

    /**
     * @brief Get the values associated with the given basis
     *
     * @return A pointer to the degrees of freedom
     */
    template <A2D::index_t index>
    const T* get() const {
      return dof[index];
    }

    // Variables for all the basis functions
    T* dof[Basis::nbasis];
  };

  /**
   * @brief Initialize the element vector values
   *
   * This function may be called once before element values are accessed.
   */
  void init_values() {
    for (A2D::index_t elem = 0; elem < mesh.get_num_elements(); elem++) {
      for (A2D::index_t basis = 0; basis < Basis::nbasis; basis++) {
        for (A2D::index_t i = 0; i < Basis::get_ndof<basis>(); i++) {
          const int sign = mesh.get_global_dof_sign(elem, basis, i);
          const A2D::index_t dof_index = mesh.get_global_dof(elem, basis, i);
          element_dof(elem, dof_index) = sign * vec[dof_index];
        }
      }
    }

    // Transfer to device
  }

  /**
   * @brief Initialize any local element vector values to zero
   *
   * This function may be called before element values are added.
   */
  void init_zero_values() {
    element_dof.zero();  // Zero element values
  }

  /**
   * @brief Finish adding values to the element vector
   *
   * Add any values from the element vector into the source vector.
   */
  void add_values() {
    for (A2D::index_t elem = 0; elem < mesh.get_num_elements(); elem++) {
      for (A2D::index_t basis = 0; basis < Basis::nbasis; basis++) {
        for (A2D::index_t i = 0; i < Basis::get_ndof<basis>(); i++) {
          const int sign = mesh.get_global_dof_sign(elem, basis, i);
          const A2D::index_t dof_index = mesh.get_global_dof(elem, basis, i);
          vec[dof_index] += sign * element_dof(elem, dof_index);
        }
      }
    }

    // Transfer to host
  }

  /**
   * @brief Get the element values from the object and store them in the FEDof
   *
   * @param elem the element index
   * @param dof the object that stores a reference to the degrees of freedom
   */
  // Get values for this element from the vector
  void get_element_values(A2D::index_t elem, FEDof& dof) {
    element_dof();

    for (A2D::index_t basis = 0; basis < Basis::nbasis; basis++) {
      dof.dof[basis] = element_dof(elem, Kokkos::ALL);
    }
  }

  /**
   * @brief Add the degree of freedom values to the element vector
   *
   * @param elem the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   * freeom
   *
   * If FEDof contains a pointer to data, this function may do nothing
   */
  // Add values for this element to the vector
  void add_element_values(A2D::index_t elem, const FEDof& dof) {}

 private:
  A2D::ElementMesh<Basis>& mesh;
  A2D::SolutionVector<T>& vec;
  Kokkos::View<T * [Basis::ndof]> element_dof;
};

}  // namespace A2D

#endif  //  A2D_FE_ELEMENT_VECTOR_H
